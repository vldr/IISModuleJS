#include "v8_wrapper.h"
#include <simdb/simdb.hpp>

namespace v8_wrapper
{
	namespace fs = std::experimental::filesystem;

	// All the global objects and functions for v8.
	v8::Global<v8::Object> global_db_object;
	v8::Global<v8::Object> global_fetch_object;

	v8::Global<v8::Object> global_http_response_object;
	v8::Global<v8::Object> global_http_request_object;
	 
	/////////////////////////////////////////////////

	v8::Global<v8::Function> function_pre_begin_request;
	v8::Global<v8::Function> function_begin_request;
	v8::Global<v8::Function> function_directory_change;
	v8::Global<v8::Function> function_send_response;

	// A persistent context for v8.
	v8::Persistent<v8::Context> context;

	// The unique isolate for v8.
	v8::Isolate * isolate = nullptr; 

	// The name of the default script to be launched. 
	std::wstring script_name;
	std::wstring app_pool_folder_name;
	fs::path fs_directory;

	// Cache containing all our Eternal names.
	std::unordered_map<
		const void*,
		std::vector<
			v8::Eternal<v8::Name>
		>
	> eternal_name_cache_;

	// Simdb database use for ipc.get and ipc.set
	simdb db;

	/**
	 * The method that initializes everything necessary.
	 */
	void start(std::wstring app_pool_name)
	{
		// Setup our engine thread.
		std::thread engine_thread([app_pool_name] {	
			db = simdb("IISModule", 1024, 4096);
			db.flush();

			///////////////////////////
			
			app_pool_folder_name = app_pool_name;

			script_name = app_pool_folder_name;
			script_name.append(L".js");

			///////////////////////////
			 
			std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
			
			v8::V8::InitializePlatform(platform.get());
			v8::V8::InitializeICU(); 
			v8::V8::Initialize();
#ifdef _DEBUG
			v8::V8::SetFlagsFromString("--always-opt --allow-natives-syntax --track-retaining-path --trace-opt --expose-gc");
#else
			v8::V8::SetFlagsFromString("--always-opt"); 
#endif
			///////////////////////////

			v8::Isolate::CreateParams create_params;
			create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

			///////////////////////////

			isolate = v8::Isolate::New(create_params);

			//////////////////////////////////////////

			load_and_watch();
		});
		engine_thread.detach();
	}

	/**
	 * Resets the engine by creating a new context.
	 */
	void reset_engine()
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);

		global_http_response_object.Reset();
		global_http_request_object.Reset();
		
		function_begin_request.Reset();
		function_directory_change.Reset();
		function_send_response.Reset();
		function_pre_begin_request.Reset();

		// Reset our context...
		context.Reset(isolate, create_shell_context());

		// Initialize our objects...
		initialize_objects();
	} 

	/**
	* Directory notify change callback.
	*/
	void directory_change_callback()
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));
		
		////////////////////////////////////////////

		if (function_directory_change.IsEmpty()) 
			return;

		////////////////////////////////////////////

		auto local_function = function_directory_change.Get(isolate);
			
		function_directory_change.Get(isolate)->Call(
			isolate->GetCurrentContext(),
			v8::Null(isolate),
			0,
			NULL
		);
	}

	/**
	* Returns the path to the filesystem directory.
	*
	* RFC 3986 5.2.4.
	* https://tools.ietf.org/html/rfc3986#section-5.2.4
	*/
	std::experimental::filesystem::path get_relative_file_path(std::wstring input)
	{
		std::wstring output;
		output.reserve(input.length());

		// 1. The input buffer is initialized with the now-appended path
		// components and the output buffer is initialized to the empty
		// string.
		while (!input.empty())
		{
			// 2.

			// A. If the input buffer begins with a prefix of "../" or "./",
			// then remove that prefix from the input buffer; otherwise,
			if (input.find(L"../", 0, 3) == 0)
			{
				input.replace(0, 3, L"");
			}
			else if (input.find(L"./", 0, 2) == 0)
			{
				input.replace(0, 2, L"");
			}

			// B. if the input buffer begins with a prefix of "/./" or "/.",
			// where "." is a complete path segment, then replace that
			// prefix with "/" in the input buffer; otherwise,
			else if (input.find(L"/./", 0, 3) == 0)
			{
				input.replace(0, 3, L"/");
			}
			else if (input == L"/.")
			{
				input.replace(0, 2, L"/");
			}

			// C. if the input buffer begins with a prefix of "/../" or "/..",
			// where ".." is a complete path segment, then replace that
			// prefix with "/" in the input buffer and remove the last
			// segment and its preceding "/" (if any) from the output
			// buffer; otherwise,
			else if (input.find(L"/../", 0, 4) == 0)
			{
				input.replace(0, 4, L"/");

				auto position = output.find_last_of(L"/");

				if (position != std::string::npos)
				{
					output = output.substr(0, position);
				}
			}
			else if (input == L"/..")
			{
				input.replace(0, 3, L"/");

				auto position = output.find_last_of(L"/");

				if (position != std::string::npos)
				{
					output = output.substr(0, position);
				}
			}

			// D. if the input buffer consists only of "." or "..", then remove
			// that from the input buffer; otherwise,
			else if (input == L"." || input == L"..")
			{
				input = L"";
			}

			// E. move the first path segment in the input buffer to the end of
			// the output buffer, including the initial "/" character(if
			// any) and any subsequent characters up to, but not including,
			// the next "/" character or the end of the input buffer.
			else
			{
				auto position = input.find(L"/");

				if (position == 0) position = input.find(L"/", position + 1);
				if (position == std::string::npos) position = input.length();

				output += input.substr(0, position);
				input = input.substr(position);
			}
		}

		// 3. Finally, the output buffer is returned as the result of
		// remove_dot_segments.
		return fs_directory / fs::path(output);
	}

	/**
	 * Gets the path to the location of 
	 * where all the scripts reside.
	 */
	std::experimental::filesystem::path get_path(std::wstring script = std::wstring())
	{
		//////////////////////////////////////////

		// String containing our documents path.
		PWSTR known_folder = nullptr;
		
		// Fetch our documents path.
		auto hr = SHGetKnownFolderPath(FOLDERID_Public, 0, (HANDLE)-1, &known_folder);

		// Check if our hr failed...
		if (FAILED(hr)) throw std::exception("unable to get known folder");

		//////////////////////////////////////////

		// Setup our path.
		fs::path path = known_folder;

		// Free our object.
		CoTaskMemFree(known_folder);

		//////////////////////////////////////////

		if (!script.empty())
		{
			// Create our script path.
			fs::path script_path = script;

			// Append our script's file name.
			path.append(script_path.filename());
		}

		/////////////////////////////////////////
		
		// Return path. 
		return path; 

		//////////////////////////////////////////
	}

	/**
	 * Returns a preexisting or creates a new eternal instance.
	 */
	const v8::Eternal<v8::Name>* find_or_create_eternal_name_cache(
		const void* lookup_key, 
		const char* const names[],
		size_t count)
	{
		auto it = eternal_name_cache_.find(lookup_key);
		const std::vector<v8::Eternal<v8::Name>>* vector = nullptr;

		if (it == eternal_name_cache_.end()) 
		{
			std::vector<v8::Eternal<v8::Name>> new_vector(count);
			std::transform(
				names, names + count, new_vector.begin(), [](const char* name) {
					return v8::Eternal<v8::Name>(isolate, v8pp::to_v8(isolate, name));
				});

			eternal_name_cache_[lookup_key] = std::move(new_vector);
			vector = &eternal_name_cache_[lookup_key];
		}
		else 
		{
			vector = &it->second;
		}
		
		return vector->data();
	}

	/**
	 * Loads an initial script file 
	 * and watches for changes every second. 
	 */
	void load_and_watch()
	{  

#ifdef _DEBUG
		// Setup our rpc server to handle remote code executions 
		// that are needed for debugging and tests.
		rpc::server rpc_server(8080);

		// Bind our execute function to actually execute our scripts.
		rpc_server.bind("execute", [](std::string script) {
			reset_engine();
			
			return execute_string((char*)script.c_str(), true, true);
		});

		// Run our rpc server asynchronously.
		rpc_server.async_run();
#endif

		//////////////////////////////////////////

		auto script_path = get_path(script_name);

		fs_directory = get_path() / app_pool_folder_name;

		//////////////////////////////////////////
		  
		HANDLE change_notify_handle = 0;

		//////////////////////////////////////////
		 
		if (!fs::is_directory(fs_directory))
		{
			vs_printf("Attempting to create a filesystem directory.\n");

			vs_printf(
				CreateDirectoryW(fs_directory.c_str(), NULL) ?
				"Successfully created a new workspace directory.\n" 
				: "Failed to create a workspace directory!\n" 
			);
		}  

		//////////////////////////////////////////

		fs::file_time_type last_write;
		std::error_code ec; 
		DWORD wait_for = 0; 

		for (;;)
		{ 
			// Check if our file exists, and last write has changed.
			if (last_write != fs::last_write_time(script_path, ec) && !ec)
			{
				last_write = fs::last_write_time(script_path);
				vs_printf("Resetting engine and loading \"%ws\" script...\n", script_name.c_str());

				reset_engine();
				execute_file(script_path.c_str());
			}   
			 
			//////////////////////////////////////////

			if (wait_for != WAIT_TIMEOUT) 
			{
				change_notify_handle = FindFirstChangeNotificationW(
					fs_directory.c_str(),
					FALSE,
					FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
				);
			}

			auto wait_for = WaitForSingleObject(
				change_notify_handle, 
				1000
			); 

			if (wait_for == WAIT_OBJECT_0)
			{ 
				Sleep(1000);

				directory_change_callback();
			}
		}	 
		
		//////////////////////////////////////////
	}

	/**
	 * Creates various global objects and methods 
	 * and creates a brand new context.
	 */
	v8::Local<v8::Context> create_shell_context()
	{
		// Setup isolate locker...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);

		// Setup our global module.
		v8pp::module global(isolate);

		// print(msg: any, ...): void
		global.set("print", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			for (int i = 0; i < args.Length(); i++)
			{
				// Get the string provided by the user.
				v8::String::Utf8Value const value(
					isolate,
					args[i]->ToDetailString(
						args.GetIsolate()->GetCurrentContext()
					).ToLocalChecked()
				);

				// Check if not first item.
				if (i != 0) vs_printf(" ");

				// Print contents provided.
				vs_printf("%s", *value);
			}

			// Print a newline.
			vs_printf("\n");
		});

		// load(fileName: String, ...): void
		global.set("load", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			for (int i = 0; i < args.Length(); i++)
			{
				// Get the name of the file provided by the user.
				auto name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[i]);

				// Get the path with our file name.
				auto path = get_path(name);

				// Execute the file in v8.
				execute_file(path.c_str());
			}
		});

		// [SIGNATURE 1]
		// register(
		//     type: CALLBACK_TYPES (Number),
		//     callback: (Function(Response, Request): number)
		// ): void
		//
		// [SIGNATURE 2]
		// register(
		//     callback: (Function(Response, Request): number)
		// ): void
		global.set("register", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1) throw std::exception("invalid function signature for register");

			////////////////////////////////////////////////

			// Backwards compatibility for the older variant of the
			// register function. Assumes that you want a BEGIN_REQUEST
			// callback.
			if (args.Length() == 1 && args[0]->IsFunction())
			{
				function_begin_request.Reset(isolate, v8::Local<v8::Function>::Cast(args[0]));

				return;
			}

			////////////////////////////////////////////////
			
			if (!args[0]->IsNumber() || !args[1]->IsFunction()) 
				throw std::exception("invalid function signature 2 for register");

			////////////////////////////////////////////////

			auto type = CALLBACK_TYPES(
				v8pp::from_v8<int>(isolate, args[0])
			);

			////////////////////////////////////////////////
			
			switch (type) 
			{
			case BEGIN_REQUEST:
				function_begin_request.Reset(isolate, v8::Local<v8::Function>::Cast(args[1]));
				break;
			case SEND_RESPONSE:
				function_send_response.Reset(isolate, v8::Local<v8::Function>::Cast(args[1]));
				break;
			case PRE_BEGIN_REQUEST:
				function_pre_begin_request.Reset(isolate, v8::Local<v8::Function>::Cast(args[1]));
				break;
			default:
				throw std::exception("invalid callback type for register");
			}
		});
		
		////////////////////////////////////////

		// http Property
		v8pp::module http_module(isolate);	

		// http.fetch(
		//	   hostname: String,
		//	   path: String, 
		//	   init: Object {optional},
		// ): Promise
		http_module.set("fetch", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 2) 
				throw std::exception("invalid function signature for http.fetch");

			///////////////////////////////////////////////
			
			if (!args[0]->IsString() || !args[1]->IsString()) 
				throw std::exception("invalid argument types for http.fetch");

			///////////////////////////////////////////////

			// Require Parameters
			
			auto hostname = v8pp::from_v8<std::string>(isolate, args[0]);
			auto path = v8pp::from_v8<std::string>(isolate, args[1]);

			///////////////////////////////////////////////

			FetchRequest fetch_request(hostname, path);

			///////////////////////////////////////////////
			
			if (args.Length() > 2 && args[2]->IsObject())
			{
				auto object = args[2].As<v8::Object>();
				
				////////////////////////////////////

				static const char* const kKeys[] = 
				{
					"body",
					"method",
					"is_ssl",
					"headers"
				};

				auto keys = find_or_create_eternal_name_cache(
					kKeys, 
					kKeys, 
					std::size(kKeys)
				);

				////////////////////////////////////
				
				auto context = isolate->GetCurrentContext();
				
				////////////////////////////////////
				
				// "body" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[0].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsString())
					{
						fetch_request.body = v8pp::from_v8<std::string>(isolate, value);
					}
				}

				////////////////////////////////////

				// "method" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[1].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsString())
					{
						fetch_request.method = v8pp::from_v8<std::string>(isolate, value);
					}
				}

				////////////////////////////////////

				// "is_ssl" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[2].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsBoolean())
					{
						fetch_request.is_ssl = v8pp::from_v8<bool>(isolate, value);
					}
				}

				////////////////////////////////////

				// "headers" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[3].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsObject())
					{
						auto object_value = value.As<v8::Object>();
					
						if (!object_value.IsEmpty())
						{
							auto prop_names = object_value->GetPropertyNames(context).ToLocalChecked();
							
							for (uint32_t i = 0, count = prop_names->Length(); i < count; ++i)
							{
								v8::Local<v8::Value> key = prop_names->Get(context, i).ToLocalChecked();
								v8::Local<v8::Value> val = object_value->Get(context, key).ToLocalChecked();

								fetch_request.headers.emplace(
									v8pp::from_v8<std::string>(isolate, key),
									v8pp::from_v8<std::string>(isolate, val)
								);
							}
						}
					}
				}
			} 

			////////////////////////////////////
			
			// Setup a resolver.
			auto resolver = v8::Promise::Resolver::New(args.GetIsolate()->GetCurrentContext()).ToLocalChecked();
			auto resolver_global = v8::Global<v8::Promise::Resolver>(args.GetIsolate(), resolver);

			// Set the return value to our promise.
			args.GetReturnValue().Set(
				resolver_global.Get(isolate)->GetPromise()
			);

			// Our request thread.
			std::thread request_thread([
				resolver = std::move(resolver_global), 
				fetch_request = std::move(fetch_request)
			] {
				std::unique_ptr<httplib::Response> response;
				 
				if (fetch_request.is_ssl)
				{
					httplib::SSLClient client(fetch_request.hostname);
					client.enable_server_certificate_verification(false);

					if (fetch_request.method == "GET")
					{
						response = client.Get(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "POST")
					{
						response = client.Post(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "HEAD")
					{
						response = client.Head(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PUT")
					{
						response = client.Put(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}
					
					if (fetch_request.method == "DELETE")
					{
						response = client.Delete(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "OPTIONS")
					{
						response = client.Options(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PATCH")
					{
						response = client.Patch(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body, nullptr);
					}
				}
				else
				{
					httplib::Client client(fetch_request.hostname);	

					if (fetch_request.method == "GET")
					{
						response = client.Get(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "POST")
					{
						response = client.Post(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "HEAD")
					{
						response = client.Head(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PUT")
					{
						response = client.Put(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "DELETE")
					{
						response = client.Delete(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "OPTIONS")
					{
						response = client.Options(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PATCH")
					{
						response = client.Patch(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body, nullptr);
					}
				}

				//////////////////////////////////////////
				
				// We should only lock once the request has finished.
				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Context::Scope context_scope(context.Get(isolate));

				// Check if our request was successful.
				if (!response)
				{
					resolver.Get(isolate)->Reject(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, "unable to fetch")
					);
					 
					return;  
				}

				//////////////////////////////////

				auto fetch_object = global_fetch_object.Get(isolate)->Clone();

				//////////////////////////////////

				auto fetch_response = new FetchResponse(isolate, fetch_object, response.release());

				//////////////////////////////////
				
				fetch_response->response_object.SetWeak(
					fetch_response,
					[](const v8::WeakCallbackInfo<FetchResponse>& data)
					{
						// Reset our JS object.
						data.GetParameter()->response_object.Reset();

						///////////////////////////////

						// Decrement our external memory usage.
						data.GetIsolate()->AdjustAmountOfExternalAllocatedMemory(
							-data.GetParameter()->capacity()
						);

						///////////////////////////////

						// Delete our object.
						delete data.GetParameter();
					},
					v8::WeakCallbackType::kParameter
				);

				// Increment our external memory usage.
				isolate->AdjustAmountOfExternalAllocatedMemory(
					fetch_response->capacity()
				);

				//////////////////////////////////

				resolver.Get(isolate)->Resolve(
					isolate->GetCurrentContext(), 
					fetch_object
				);
			});

			request_thread.detach();
		});

		////////////////////////////////////////

		// ipc Property
		v8pp::module ipc_module(isolate);

		// ipc.set(key: String, value: any): void
		ipc_module.set("set", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 2) 
				throw std::exception("invalid function signature for ipc.set");
			
			if (!args[0]->IsString()) 
				throw std::exception("invalid first parameter, must be a string for ipc.set");

			/////////////////////////////////////////////

			auto key = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
			 
			/////////////////////////////////////////////

			SerializerDelegate serializer_delegate(args.GetIsolate());
			v8::ValueSerializer serializer(args.GetIsolate(), &serializer_delegate);
			
			auto result = serializer.WriteValue(
				args.GetIsolate()->GetCurrentContext(),
				args[1]
			).FromMaybe(false);

			if (!result) throw std::exception("invalid object given, unable to serialize for ipc.set");

			/////////////////////////////////////////////
			
			std::pair<uint8_t*, size_t> buffer = serializer.Release();
			
			result = db.put(key.data(), key.length(), buffer.first, buffer.second);
			
			serializer_delegate.FreeBufferMemory(buffer.first);
		});

		// ipc.get(key: String): any || null
		ipc_module.set("get", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			v8::EscapableHandleScope escapable_handle_scope(args.GetIsolate()); 
			
			if (args.Length() < 1) throw std::exception("invalid function signature for ipc.get");
			if (!args[0]->IsString()) throw std::exception("invalid first parameter, must be a string for ipc.get");

			auto key = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			/////////////////////////////////////////////

			simdb::u32 len = 0;

			db.len(key, &len);

			if (!len) RETURN_NULL

			/////////////////////////////////////////////

			auto buffer = std::make_unique<uint8_t[]>(len);

			if (!buffer)
			{
				throw std::exception("unable to allocate for ipc.get");
			}

			auto result = db.get(
				key.c_str(),
				buffer.get(),
				len
			);
			
			if (!result) RETURN_NULL
			
			/////////////////////////////////////////////

			DeserializerDelegate deserializer_delegate(args.GetIsolate());
			v8::ValueDeserializer deserializer(
				args.GetIsolate(), 
				buffer.get(),
				len,
				&deserializer_delegate
			);

			auto value = deserializer.ReadValue(args.GetIsolate()->GetCurrentContext());

			if (value.IsEmpty()) throw std::exception("unable to deserialize value for ipc.get");

			/////////////////////////////////////////////
			
			args.GetReturnValue().Set(
				escapable_handle_scope.Escape(
					value.ToLocalChecked()
				)
			); 
		});

		////////////////////////////////////////
		  
		// fs Property 
		v8pp::module fs_module(isolate);
		    
		// fs.register(callback: Function): void
		fs_module.set("register", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1) throw std::exception("invalid function signature for fs.register");

			////////////////////////////////////////////////

			function_directory_change.Reset(isolate, v8::Local<v8::Function>::Cast(args[0]));
		});

		// fs.write(fileName: String, content: String || Uint8Array, append: boolean {optional, default: false}): void 
		fs_module.set("write", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 2)
				throw std::exception("invalid function signature for fs.write");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for fs.write");

			/////////////////////////////////////////////

			auto file_name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[0]);
			auto file_path = get_relative_file_path(file_name);

			// Whether or not we should simply append to our file
			auto append = args.Length() > 2 && args[2]->IsBoolean() && v8pp::from_v8<bool>(args.GetIsolate(), args[2]);

			/////////////////////////////////////////////

			// Attempt to open a handle to the file.
			auto file = _wfopen(file_path.c_str(), append ? L"ab" : L"wb");

			// Throw an exception if we were unable to open the file.
			if (file == nullptr)
				throw std::exception("unable to open file handle.");

			/////////////////////////////////////////////

			if (args[1]->IsString())
			{
				// Get the utf8 value from our string.
				v8::String::Utf8Value const utf8_string(isolate, args[1]);

				// Write the contents of the string to the file.
				fwrite(*utf8_string, sizeof(unsigned char), utf8_string.length(), file);
			}
			else if (args[1]->IsUint8Array())
			{
				// Get the uint8array buffer contents.
				auto uint8array_buffer = args[1].As<v8::Uint8Array>()->Buffer()->GetContents();

				// Write the contents of the buffer to the file.
				fwrite(uint8array_buffer.Data(), sizeof(unsigned char), uint8array_buffer.ByteLength(), file);
			}
			else throw std::exception("invalid data type provided.");

			// Close our handle to the file.
			fclose(file);
		});

		// fs.read(fileName: String, asArray: bool {optional, default: false}): String || Uint8Array || null
		fs_module.set("read", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for fs.read");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for fs.read");

			/////////////////////////////////////////////
			 
			auto file_name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[0]);
			auto file_path = get_relative_file_path(file_name); 

			/////////////////////////////////////////////

			// Attempt to open a handle to the file.
			auto file = _wfopen(file_path.c_str(), L"rb");

			// Throw an exception if we were unable to open the file.
			if (file == nullptr)
				throw std::exception("unable to open file handle.");

			/////////////////////////////////////////////

			fseek(file, 0, SEEK_END);
			size_t file_size = ftell(file);
			rewind(file); 

			/////////////////////////////////////////////

			auto as_array = args.Length() > 1 && args[1]->IsBoolean() && v8pp::from_v8<bool>(args.GetIsolate(), args[1]);

			if (as_array)
			{
				auto array_buffer = v8::ArrayBuffer::New(
					isolate,
					file_size
				);

				////////////////////////////////////////////////

				fread(
					array_buffer->GetContents().Data(), 
					sizeof(unsigned char), 
					file_size, 
					file
				);

				////////////////////////////////////////////////

				auto uint8_array = v8::Uint8Array::New(
					array_buffer,
					0,
					file_size
				);

				////////////////////////////////////////////////

				args.GetReturnValue().Set(
					uint8_array
				);
			}
			else
			{
				auto external_string = new ExternalString(file_size);

				////////////////////////////////////////////////

				fread(
					(void*)external_string->data(),
					sizeof(unsigned char),
					file_size,
					file 
				);

				////////////////////////////////////////////////

				auto string = v8::String::NewExternalOneByte(isolate, external_string);

				////////////////////////////////////////////////

				args.GetReturnValue().Set(
					string.ToLocalChecked()
				);
			}

			fclose(file);
		}); 

		////////////////////////////////////////

		// db Property 
		v8pp::module db_module(isolate);
		 
		// db.init(connectionInfo: String): db Object
		db_module.set("init", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for db.init");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for db.init");

			/////////////////////////////////////////////

			auto connection_info = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			/////////////////////////////////////////////

			auto db_context = std::make_unique<DbContext>();
			db_context.get()->session.open(connection_info);

			//////////////////////////////////

			auto db_object = global_db_object.Get(isolate)->Clone();
			auto db_handler = new DbHandler(isolate, db_object, db_context.release());

			//////////////////////////////////

			db_handler->db_object.SetWeak(
				db_handler,
				[](const v8::WeakCallbackInfo<DbHandler>& data)
				{
					// Reset our JS object.
					data.GetParameter()->db_object.Reset();

					///////////////////////////////

					// Decrement our external memory usage.
					isolate->AdjustAmountOfExternalAllocatedMemory(
						-data.GetParameter()->capacity()
					);

					///////////////////////////////

					// Delete our object.
					delete data.GetParameter();
				},
				v8::WeakCallbackType::kParameter
			);

			//////////////////////////////////

			// Increment our external memory usage.
			isolate->AdjustAmountOfExternalAllocatedMemory(
				db_handler->capacity()
			);

			//////////////////////////////////

			args.GetReturnValue().Set(
				db_object
			);
		});

		////////////////////////////////////////

		// ipc Object
		global.set_const("ipc", ipc_module);

		// http Object
		global.set_const("http", http_module);

		// fs Object
		global.set_const("fs", fs_module);

		// db Object
		global.set_const("db", db_module);

		////////////////////////////////////////

		return v8::Context::New(isolate, nullptr, global.obj_);
	}

	/**
	 * Initializes global objects.
	 */
	void initialize_objects()
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate); 
		v8::Context::Scope context_scope(context.Get(isolate));

		/////////////////////////////
		//      DB JS Object       //
		/////////////////////////////
		
		if (global_db_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate);

			// Setup our functions

			// prepare(query: String): void
			module.set("prepare", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for prepare");

				if (args.Length() < 1)
					throw std::exception("invalid function signature for prepare");

				if (!args[0]->IsString())
					throw std::exception("invalid first parameter, must be a string for prepare");

				/////////////////////////////////////////////

				auto query = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

				/////////////////////////////////////////////

				DB_CONTEXT->statement = DB_CONTEXT->session.prepare(query);
			});

			// reset(): void
			module.set("reset", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for reset");

				/////////////////////////////////////////////

				DB_CONTEXT->statement.reset();
			});

			// exec(): void
			module.set("exec", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for exec");

				/////////////////////////////////////////////

				DB_CONTEXT->statement.exec();
			}); 

			// query(): void
			module.set("query", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for query");
				 
				/////////////////////////////////////////////

				DB_CONTEXT->result = DB_CONTEXT->statement.query();
			}); 

			// queryRow(): void
			module.set("queryRow", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for row");

				/////////////////////////////////////////////

				DB_CONTEXT->result = DB_CONTEXT->statement.row();
			});
			 
			// close(): void
			module.set("close", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for close");

				/////////////////////////////////////////////

				DB_CONTEXT->session.close();
				DB_CONTEXT->session = DbSession();
				DB_CONTEXT->statement = cppdb::statement();
				DB_CONTEXT->result = cppdb::result::result();
			});

			// next(): bool
			module.set("next", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for next");

				/////////////////////////////////////////////

				RETURN_THIS(
					DB_CONTEXT->result.next()
				)
			}); 

			// [SIGNATURE 1]
			// fetch(dataType: DB_DATA_TYPES, name: String): Number | String | boolean | null | Uint8Array
			//
			// [SIGNATURE 2]
			// fetch(dataType: DB_DATA_TYPES, col: Number): Number | String | boolean | null | Uint8Array
			module.set("fetch", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for fetch");
				 
				if (args.Length() < 2)
					throw std::exception("not enough arugments for fetch");

				if (!args[0]->IsInt32())
					throw std::exception("invalid first parameter must be an integer (32-bit only)");

				///////////////////////////////////////////// 

				int data_type = v8pp::from_v8<int>(args.GetIsolate(), args[0]);
				
				///////////////////////////////////////////// 

				// Default variables to be filled in according to the signature.
				int input_col_index = 0;
				std::string input_col_name = std::string();

				///////////////////////////////////////////// 

				// Boolean representing the input type.
				bool input_type = 0;

				/////////////////////////////////////////////
				
				if (args[1]->IsString()) 
				{
					input_type = false;
					input_col_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);
				}
				else if (args[1]->IsInt32()) 
				{
					input_type = true;
					input_col_index = v8pp::from_v8<int>(args.GetIsolate(), args[1]);
				}
				else 
					throw std::exception("invalid second parameter must be an integer (32-bit only) or string");

				/////////////////////////////////////////////
				
				if (input_type ? DB_CONTEXT->result.is_null(input_col_index) : DB_CONTEXT->result.is_null(input_col_name))
					RETURN_NULL

				/////////////////////////////////////////////

				switch (data_type)
				{
				case DB_DATA_TYPES::STRING:
					RETURN_THIS(
						input_type ? DB_CONTEXT->result.get<std::string>(input_col_index)
						: DB_CONTEXT->result.get<std::string>(input_col_name)
					)
					break;
				case DB_DATA_TYPES::INTEGER:
					RETURN_THIS(
						input_type ? DB_CONTEXT->result.get<int>(input_col_index)
						: DB_CONTEXT->result.get<int>(input_col_name)
					)
					break;
				case DB_DATA_TYPES::DOUBLE:
					RETURN_THIS(
						input_type ? DB_CONTEXT->result.get<double>(input_col_index)
						: DB_CONTEXT->result.get<double>(input_col_name)
					)
					break;
				case DB_DATA_TYPES::BOOL:
					RETURN_THIS(
						(bool)(input_type ? DB_CONTEXT->result.get<int>(input_col_index)
						: DB_CONTEXT->result.get<int>(input_col_name))
					)
					break;
				default:
					throw std::exception("unknown data type for fetch");
					break;
				}  
			});

			// [SIGNATURE 1]
			// bind(value: Number | String | boolean | null): void
			// 
			// [SIGNATURE 2]
			// bind(index: Number {32-bit integer only}, value: Number | String | boolean | null): void
			module.set("bind", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) 
					throw std::exception("invalid db context for bind");

				if (args.Length() < 1) 
					throw std::exception("not enough arugments for bind");
		
				/////////////////////////////////////////////

				bool with_index = args.Length() > 1;
				auto input_value = args[with_index]; 

				// Throw an exception if we were provided an index 
				// but it isn't the correct data type.
				if (with_index && !args[0]->IsInt32()) 
					throw std::exception("invalid index type for bind");

				// Get our index if provided.
				int index = with_index ? v8pp::from_v8<int>(args.GetIsolate(), args[0]) : 0;

				/////////////////////////////////////////////

				// Handle string data binding.
				if (input_value->IsString())
				{
					auto value = v8pp::from_v8<std::string>(args.GetIsolate(), input_value);

					if (with_index) DB_CONTEXT->statement.bind(index, value);
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(value);
				}
				// Handle 32-bit integer data binding.
				else if (input_value->IsInt32())
				{
					auto value = v8pp::from_v8<int>(args.GetIsolate(), input_value);

					if (with_index) DB_CONTEXT->statement.bind(index, value);
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(value);
				}
				// Handle boolean data binding.
				else if (input_value->IsBoolean())
				{ 
					auto value = v8pp::from_v8<bool>(args.GetIsolate(), input_value);

					if (with_index) DB_CONTEXT->statement.bind(index, value);
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(value);
				}
				// Handle double data binding.
				else if (input_value->IsNumber())
				{
					auto value = v8pp::from_v8<double>(args.GetIsolate(), input_value);

					if (with_index) DB_CONTEXT->statement.bind(index, value);
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(value);
				}
				// Handle null or undefined data binding.
				else if (input_value->IsNullOrUndefined())
				{
					if (with_index) DB_CONTEXT->statement.bind_null(index);
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind_null();
				}
				// Attempt to call toString on the object.
				else
				{
					v8::Local<v8::String> to_string;

					//////////////////////////////////////

					auto result = input_value->ToString(args.GetIsolate()->GetCurrentContext())
						.ToLocal(&to_string); 

					if (!result)
						throw std::exception("invalid type provided.");

					//////////////////////////////////////

					auto value = v8pp::from_v8<std::string>(args.GetIsolate(), to_string);

					if (with_index) DB_CONTEXT->statement.bind(index, value);
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(value);
				}
			});

			// Set our internal field count.
			module.obj_->SetInternalFieldCount(1);

			// Reset our pointer...
			global_db_object.Reset(isolate, module.new_instance());
		}
		
		/////////////////////////////
		// FetchResponse JS Object //
		/////////////////////////////
		if (global_fetch_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate); 

			// Setup our functions

			// status(): number
			module.set("status", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for status");

				RETURN_THIS(FETCH_RESPONSE->status)
			});

			// text(): String || null
			module.set("text", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for text");

				////////////////////////////////////////////////

				if (FETCH_RESPONSE->body.empty()) RETURN_NULL;
				 
				////////////////////////////////////////////////  

				auto external_string = new ExternalString(FETCH_RESPONSE->body.size());

				//////////////////////////////////////////////// 

				std::memcpy(
					(void*)external_string->data(),
					FETCH_RESPONSE->body.data(),
					FETCH_RESPONSE->body.size()
				);

				////////////////////////////////////////////////

				auto string = v8::String::NewExternalOneByte(isolate, external_string);

				////////////////////////////////////////////////

				args.GetReturnValue().Set(
					string.ToLocalChecked()
				); 
			});

			// blob(): Uint8Array || null
			module.set("blob", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for blob");

				////////////////////////////////////////////////

				auto body = FETCH_RESPONSE->body;

				////////////////////////////////////////////////

				if (body.empty()) RETURN_NULL;

				////////////////////////////////////////////////

				auto array_buffer = v8::ArrayBuffer::New(
					isolate,
					body.size()
				);

				////////////////////////////////////////////////

				std::memcpy(
					array_buffer->GetContents().Data(),
					body.data(),
					body.size()
				);

				////////////////////////////////////////////////

				auto uint8_array = v8::Uint8Array::New(
					array_buffer,
					0,
					body.size()
				);

				////////////////////////////////////////////////

				args.GetReturnValue().Set(uint8_array);
			});

			// headers(): Object<String, String> || null
			module.set("headers", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for headers");

				////////////////////////////////////////////////

				auto headers = FETCH_RESPONSE->headers;

				////////////////////////////////////////////////
				
				if (headers.empty()) RETURN_NULL;
				
				////////////////////////////////////////////////
				
				auto headers_object = v8::Object::New(isolate);

				////////////////////////////////////////////////
				
				for (auto header : headers)
				{
					headers_object->Set(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, header.first),
						v8pp::to_v8(isolate, header.second)
					);
				}
				
				////////////////////////////////////////////////
				
				args.GetReturnValue().Set(
					headers_object
				);
			});

			// Set our internal field count.
			module.obj_->SetInternalFieldCount(1);

			// Reset our pointer...
			global_fetch_object.Reset(isolate, module.new_instance());
		}
		
		////////////////////////////
		// HttpResponse JS Object //
		////////////////////////////
		if (global_http_response_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate);

			// Setup our functions
			 
			// clear(): void
			module.set("clear", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for clear");

				HTTP_RESPONSE->Clear();
			});		

			// clearHeaders(): void
			module.set("clearHeaders", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for clearHeaders");

				HTTP_RESPONSE->ClearHeaders();
			});

			// closeConnection(): void
			module.set("closeConnection", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for closeConnection");
					
				HTTP_RESPONSE->CloseConnection();
			});

			// disableBuffering(): void
			module.set("disableBuffering", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for disableBuffering");

				HTTP_RESPONSE->DisableBuffering();
			});		
			
			// setNeedDisconnect(): void
			module.set("setNeedDisconnect", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for setNeedDisconnect");
					
				HTTP_RESPONSE->SetNeedDisconnect();
			});

			// getKernelCacheEnabled(): bool
			module.set("getKernelCacheEnabled", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for getKernelCacheEnabled");

				return bool(HTTP_RESPONSE->GetKernelCacheEnabled());
			});	

			// resetConnection(): void
			module.set("resetConnection", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for resetConnection");
					
				HTTP_RESPONSE->ResetConnection(); 
			});
							
			// getStatus(): Number
			module.set("getStatus", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for getStatus");

				// Our status code...
				USHORT status_code = 0;

				// Get our status code...
				HTTP_RESPONSE->GetStatus(&status_code);

				// Return our result. 
				return status_code; 
			});

			// setStatus(statusCode: Number, statusMessage: String): void
			module.set("setStatus", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for setStatus");

				////////////////////////////////
				
				if (args.Length() < 2) throw std::exception("invalid signature for setStatus");

				////////////////////////////////

				auto status_code = v8pp::from_v8<int>(args.GetIsolate(), args[0]);
				auto status_message
					= v8pp::from_v8<const char*>(args.GetIsolate(), args[1]);
				
				////////////////////////////////

				auto hr = HTTP_RESPONSE->SetStatus(status_code, status_message);

				////////////////////////////////

				if (FAILED(hr)) throw std::exception("failed to setStatus");
			}); 
			

			// redirect(url: String, resetStatusCode: bool, includeParameters: bool): void
			module.set("redirect", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for redirect");

				////////////////////////////////
				
				if (args.Length() < 3) throw std::exception("invalid signature for redirect");

				////////////////////////////////

				auto url = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				auto reset_status_code = v8pp::from_v8<bool>(args.GetIsolate(), args[1]); 
				auto include_parameters = v8pp::from_v8<bool>(args.GetIsolate(), args[2]);
				
				////////////////////////////////

				auto hr  = HTTP_RESPONSE->Redirect(
					url.c_str(),
					reset_status_code,
					include_parameters
				);

				////////////////////////////////

				if (FAILED(hr)) throw std::exception("failed to redirect");
			}); 

			// setErrorDescription(decription: String, shouldHtmlEncode: bool): void
			module.set("setErrorDescription", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for setErrorDescription");

				////////////////////////////////
				
				if (args.Length() < 2) throw std::exception("invalid signature for setErrorDescription");

				////////////////////////////////

				auto description = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[0]);
				auto should_html_encode = v8pp::from_v8<bool>(args.GetIsolate(), args[1]);

				////////////////////////////////
				
				auto hr= HTTP_RESPONSE->SetErrorDescription(
					description.c_str(),
					description.length(), 
					should_html_encode
				);

				if (FAILED(hr)) throw std::exception("failed to set error description");
			});

			// disableKernelCache(reason: Number): void
			module.set("disableKernelCache", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for disableKernelCache");

				////////////////////////////////
				
				if (args.Length() < 1) throw std::exception("invalid signature for disableKernelCache");

				////////////////////////////////

				auto reason = v8pp::from_v8<int>(args.GetIsolate(), args[0]);

				////////////////////////////////

				HTTP_RESPONSE->DisableKernelCache(
					reason
				); 
			});

			// deleteHeader(headerName: String): void
			module.set("deleteHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for deleteHeader");

				////////////////////////////////
				
				if (args.Length() < 1) throw std::exception("invalid signature for deleteHeader");

				////////////////////////////////

				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

				////////////////////////////////

				auto hr = HTTP_RESPONSE->DeleteHeader(
					header_name.c_str()
				);
				
				////////////////////////////////

				if (FAILED(hr)) throw std::exception("failed to delete header");
			});

			// getHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for getHeader");

				////////////////////////////////

				if (args.Length() < 1) throw std::exception("invalid signature for getHeader");

				////////////////////////////////

				v8::String::Utf8Value const header_name(isolate, args[0]);

				////////////////////////////////

				if (!*header_name) RETURN_NULL

				////////////////////////////////
					 
				USHORT header_value_count = 0; 

				////////////////////////////////

				auto header_value = HTTP_RESPONSE->GetHeader(*header_name, &header_value_count);

				////////////////////////////////
				 
				if (!header_value) RETURN_NULL

				////////////////////////////////

				auto string = v8::String::NewFromUtf8(
					isolate, header_value,
					v8::NewStringType::kNormal,
					header_value_count 
				)
				.ToLocalChecked();

				////////////////////////////////

				args.GetReturnValue().Set(string);
			}); 

			// read(asArray: bool {optional}): String || Uint8Array || null
			module.set("read", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for read");

				////////////////////////////////////////////////

				auto chunk_count = HTTP_RESPONSE->GetRawHttpResponse()->EntityChunkCount;

				////////////////////////////////////////////////

				if (!chunk_count) RETURN_NULL

				////////////////////////////////////////////////

				size_t total_size = 0;
				
				for (unsigned int i = 0; i < chunk_count; i++)
				{
					auto chunk = HTTP_RESPONSE->GetRawHttpResponse()->pEntityChunks[i];

					////////////////////////////////////////////////

					if (chunk.DataChunkType != HttpDataChunkFromMemory) continue;

					////////////////////////////////////////////////

					total_size += chunk.FromMemory.BufferLength;
				}

				if (!total_size) RETURN_NULL

				////////////////////////////////////////////////			

				bool asArray = v8pp::from_v8<bool>(isolate, args[0], false);

				////////////////////////////////////////////////

				if (asArray)
				{
					auto array_buffer = v8::ArrayBuffer::New(
						isolate,
						total_size
					);

					////////////////////////////////////////////////

					size_t offset = 0;
					
					for (unsigned int i = 0; i < chunk_count; i++)
					{
						auto chunk = HTTP_RESPONSE->GetRawHttpResponse()->pEntityChunks[i];

						////////////////////////////////////////////////

						if (chunk.DataChunkType != HttpDataChunkFromMemory) continue;
						if (!chunk.FromMemory.BufferLength) continue;

						////////////////////////////////////////////////

						std::memcpy(
							(uint8_t*)array_buffer->GetContents().Data() + offset,
							chunk.FromMemory.pBuffer,
							chunk.FromMemory.BufferLength
						);

						////////////////////////////////////////////////
						
						offset += chunk.FromMemory.BufferLength;
					}	

					////////////////////////////////////////////////

					auto uint8_array = v8::Uint8Array::New(
						array_buffer,
						0,
						total_size
					);

					////////////////////////////////////////////////

					args.GetReturnValue().Set(
						uint8_array
					);
				}
				else
				{
					auto external_string = new ExternalString(total_size);

					////////////////////////////////////////////////

					size_t offset = 0;
					
					for (unsigned int i = 0; i < chunk_count; i++)
					{
						auto chunk = HTTP_RESPONSE->GetRawHttpResponse()->pEntityChunks[i];

						////////////////////////////////////////////////

						if (chunk.DataChunkType != HttpDataChunkFromMemory) continue;
						if (!chunk.FromMemory.BufferLength) continue;

						////////////////////////////////////////////////

						std::memcpy(
							(uint8_t*)external_string->data() + offset,
							chunk.FromMemory.pBuffer,
							chunk.FromMemory.BufferLength
						);

						////////////////////////////////////////////////

						offset += chunk.FromMemory.BufferLength;
					}

					////////////////////////////////////////////////

					auto string = v8::String::NewExternalOneByte(isolate, external_string);

					////////////////////////////////////////////////

					if (string.IsEmpty()) throw std::exception("failed to obtain string");
					
					////////////////////////////////////////////////

					args.GetReturnValue().Set(
						string.ToLocalChecked()
					);
				}
			});
			
			// write(body: String || Uint8Array, mimetype: String {optional}, contentEncoding: String {optional}): void
			module.set("write", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for write");

				// Check arguments.
				if (args.Length() < 1) throw std::exception("invalid signature for write");

				////////////////////////////////////////////////

				// An empty v8 array buffer which will represent our Uint8Array in javascript.
				Utf8ValueScoped string(isolate, args[0]);
				ArrayBufferScoped array_buffer(args[0]);

				// A constant representing the maximum bytes per HTTP_CHUNK_DATA.
				constexpr unsigned long MAX_BYTES = 65535;

				// Setup our length and buffer which will
				// be used to deliver our content.
				void* buffer = nullptr;
				unsigned long buffer_size = 0;

				// Check if a string was provided.
				if (string)
				{
					buffer = string;
					buffer_size = string;
				}
				// Or if an array was provided.
				else if (array_buffer)
				{
					buffer = array_buffer;
					buffer_size = array_buffer;
				}
				else 
					throw std::exception("invalid first argument type for write");
				 
				////////////////////////////////////////////////

				if (args.Length() >= 2 && args[1]->IsString())
				{
					// Get our mimetype.
					v8::String::Utf8Value mime_type(isolate, args[1]);

					// Check the length of the mime type.
					if (!*mime_type) throw std::exception("second argument is invalid for write");

					// Clear and set our header...
					HTTP_RESPONSE->SetHeader(HttpHeaderContentType, *mime_type, mime_type.length(), TRUE);
				}
				else
					HTTP_RESPONSE->SetHeader(HttpHeaderContentType, "text/html", strlen("text/html"), TRUE);

				////////////////////////////////////////////////

				// Check if content encoding parameter was provided.
				if (args.Length() >= 3 && args[2]->IsString()) 
				{
					// Get our mimetype.
					v8::String::Utf8Value content_encoding(isolate, args[2]);

					// Check the length of the mime type.
					if (!*content_encoding) throw std::exception("third argument is invalid for write");

					// Clear and set our header...
					HTTP_RESPONSE->SetHeader(HttpHeaderContentEncoding, *content_encoding, content_encoding.length(), TRUE);
				}
				  
				////////////////////////////////////////////////

				unsigned long buffer_offset = 0;
				unsigned long bytes_to_write = pmin(pmax(buffer_size - buffer_offset, 0), MAX_BYTES);
				bool has_more_data = buffer_size - bytes_to_write > 0;

				// Loop until we write all our data.
				do 
				{ 
					// Create an array of data chunks.
					HTTP_DATA_CHUNK data_chunk = HTTP_DATA_CHUNK();
					unsigned long cb_sent = 0;
					 
					// Set the chunk to a chunk in memory.
					data_chunk.DataChunkType = HttpDataChunkFromMemory;
					data_chunk.FromMemory.pBuffer = PVOID((unsigned char*)buffer + buffer_offset);
					data_chunk.FromMemory.BufferLength = USHORT(bytes_to_write);

					// Insert the data chunks into the response.
					auto hr = HTTP_RESPONSE->WriteEntityChunks(&data_chunk, 1, FALSE, has_more_data, &cb_sent);

					// Check if our result was not successful.
					if (FAILED(hr)) throw std::exception("failed to write");

					////////////////////////////////////////

					buffer_offset += bytes_to_write;
					bytes_to_write = pmin(pmax(buffer_size - buffer_offset, 0), MAX_BYTES);
					has_more_data = buffer_size - buffer_offset > 0;
					
				} while (has_more_data);
			});

			// setHeader(headerName: String, headerValue: String, shouldReplace: bool {optional}): void
			module.set("setHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_RESPONSE) throw std::exception("invalid p_http_response for setHeader");

				////////////////////////////////

				if (args.Length() < 2) throw std::exception("invalid signature for setHeader");

				////////////////////////////////

				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				auto header_value = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);
				auto should_replace = v8pp::from_v8<bool>(args.GetIsolate(), args[2], true);

				////////////////////////////////

				auto hr = HTTP_RESPONSE->SetHeader(
					header_name.c_str(),
					header_value.c_str(), 
					header_value.length(), 
					should_replace
				);

				////////////////////////////////

				if (FAILED(hr)) throw std::exception("failed to set header");
			});

			// Set our internal field count.
			module.obj_->SetInternalFieldCount(1);

			// Reset our pointer...
			global_http_response_object.Reset(isolate, module.new_instance());
		}

		////////////////////////////////////////////////

		///////////////////////////
		// HttpRequest JS Object //
		///////////////////////////
		if (global_http_request_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate);
			 
			// Setup our functions
			
			// read(rewrite: bool {optional}): String || null
			module.set("read", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for read");

				////////////////////////////////

				// Return 'null' if there isn't any bytes to read.
				if (HTTP_REQUEST->GetRemainingEntityBytes() == 0) RETURN_NULL

				////////////////////////////////

				constexpr size_t BUFFER_SIZE = 4096;
				
				std::vector<char> bytes;
				bytes.reserve(BUFFER_SIZE);

				// Since we cannot read the entire entity body in whole
				// we will receive data in chunks of BUFFER_SIZE bytes.
				// Thus we will need to use GetRemainingEntityBytes
				// to check how many bytes are left for us to insert into
				// 'bytes'.
				auto remaining_bytes = HTTP_REQUEST->GetRemainingEntityBytes();

				////////////////////////////////

				// Loop until the remaining bytes is zero.
				while (remaining_bytes != 0)
				{
					uint8_t buffer[BUFFER_SIZE];
					DWORD read_bytes = 0;

					////////////////////////////////

					// Attempt to read the entity body synchronously.
					auto hr = HTTP_REQUEST->ReadEntityBody(buffer, sizeof buffer, FALSE, &read_bytes);

					////////////////////////////////

					// Check if we read any bytes and that
					// our reading operation didn't fail.
					if (!read_bytes || FAILED(hr)) throw std::exception("failed to read entity body");

					////////////////////////////////
					
					bytes.insert(bytes.end(), buffer, buffer + read_bytes);

					////////////////////////////////

					// Update the remaining bytes.
					remaining_bytes = HTTP_REQUEST->GetRemainingEntityBytes();
				}
				
				////////////////////////////////

				// Convert byte array to v8::String
				auto string_object = v8pp::to_v8(isolate, bytes.data(), bytes.size());

				///////////////////////////////

				// rewrite: bool {optional}
				if (args.Length() >= 1 && v8pp::from_v8<bool>(isolate, args[0]))
				{
					auto context_buffer = HTTP_CONTEXT->AllocateRequestMemory(bytes.size());

					///////////////////////////////
					
					if (!context_buffer)  throw std::exception("invalid allocation pointer for read.");

					///////////////////////////////
					
					std::memcpy(context_buffer, bytes.data(), bytes.size());

					///////////////////////////////
					
					auto hr = HTTP_REQUEST->InsertEntityBody(context_buffer, bytes.size());

					///////////////////////////////

					if (FAILED(hr)) throw std::exception("failed to rewrite");
				}
				
				///////////////////////////////

				// Set the return value.
				args.GetReturnValue().Set(
					string_object
				);
			});

			// setUrl(url: String, resetQueryString: bool {optional}): void
			module.set("setUrl", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for setUrl");

				////////////////////////////////

				if (args.Length() < 1) throw std::exception("invalid signature for setUrl");

				////////////////////////////////
				
				if (!args[0]->IsString()) throw std::exception("first parameter must be a string.");

				////////////////////////////////
				 
				auto url = v8pp::from_v8<std::string>(isolate, args[0]);
				auto resetQueryString = v8pp::from_v8<bool>(isolate, args[1], true);

				////////////////////////////////
				
				auto hr = HTTP_REQUEST->SetUrl(url.c_str(), url.length(), resetQueryString);

				////////////////////////////////

				if (FAILED(hr)) throw std::exception("failed to set url");
			});	

			// deleteHeader(headerName: String): void
			module.set("deleteHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for deleteHeader");

				///////////////////////////////
				
				if (args.Length() < 1) throw std::exception("invalid signature for deleteHeader");

				///////////////////////////////

				auto header_name = v8pp::from_v8<std::string>(isolate, args[0]);

				///////////////////////////////

				auto hr = HTTP_REQUEST->DeleteHeader(
					header_name.c_str()
				);

				///////////////////////////////
				
				if (FAILED(hr)) throw std::exception("failed to delete header");
			});

			// setHeader(headerName: String, headerValue: String, shouldReplace: bool {optional}): void
			module.set("setHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for setHeader");

				////////////////////////////////

				if (args.Length() < 2) throw std::exception("invalid signature for setHeader");

				////////////////////////////////

				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				auto header_value = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);
				auto should_replace = v8pp::from_v8<bool>(args.GetIsolate(), args[2], true);

				////////////////////////////////
				auto hr = HTTP_REQUEST->SetHeader(
					header_name.c_str(),
					header_value.c_str(), 
					header_value.length(), 
					should_replace
				);

				////////////////////////////////

				if (FAILED(hr)) throw std::exception("failed to set header");
			});

			// getMethod(): String
			module.set("getMethod", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getMethod");

				RETURN_THIS(
					std::string(HTTP_REQUEST->GetHttpMethod())
				)
			});

			// getAbsPath(): String
			module.set("getAbsPath", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getMethod");

				RETURN_THIS(
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pAbsPath,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.AbsPathLength / sizeof(wchar_t)
					)
				)
			});
			 
			// getFullUrl(): String
			module.set("getFullUrl", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getFullUrl");

				RETURN_THIS(
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pFullUrl,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.FullUrlLength / sizeof(wchar_t)
					)
				)
			}); 

			// getQueryString(): String
			module.set("getQueryString", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getQueryString");

				RETURN_THIS( 
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pQueryString,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.QueryStringLength / sizeof(wchar_t)
					)
				)
			});

			// getHost(): String
			module.set("getHost", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getHost");

				RETURN_THIS(
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pHost,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.HostLength / sizeof(wchar_t)
					)
				)
			});

			// getLocalAddress(): String
			module.set("getLocalAddress", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our pointer is valid...
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getLocalAddress");

				RETURN_THIS(
					sock_to_ip(
						HTTP_REQUEST->GetLocalAddress()
					)
				)
			}); 

			// getRemoteAddress(): String
			module.set("getRemoteAddress", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our pointer is valid...
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getRemoteAddress");
				
				RETURN_THIS(
					sock_to_ip(
						HTTP_REQUEST->GetRemoteAddress()
					)
				) 
			});

			// getHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getHeader");
				
				////////////////////////////////

				if (args.Length() < 1) throw std::exception("invalid signature for getHeader");

				////////////////////////////////

				v8::String::Utf8Value const header_name(isolate, args[0]);

				////////////////////////////////

				if (!*header_name) RETURN_NULL

				////////////////////////////////
				
				USHORT header_value_count = 0;

				////////////////////////////////
				
				auto header_value = HTTP_REQUEST->GetHeader(*header_name, &header_value_count);
                
				////////////////////////////////

				if (!header_value) RETURN_NULL

				////////////////////////////////

				auto string = v8::String::NewFromUtf8(
					isolate, header_value,
					v8::NewStringType::kNormal,
					header_value_count
				)
				.ToLocalChecked();

				////////////////////////////////

				args.GetReturnValue().Set(string);
			});

			module.obj_->SetInternalFieldCount(1);

			// Reset our pointer...
			global_http_request_object.Reset(isolate, module.new_instance());
		}
	}
	 
	/**
	 * Handles a callback that is registered in JS.
	 */
	int handle_callback(CALLBACK_TYPES type, IHttpContext * pHttpContext, void * pObject)
	{
		if (!isolate) return 0 /* CONTINUE */;

		////////////////////////////////////////////////

		v8::Global<v8::Function> * callback_function = nullptr;

		////////////////////////////////////////////////
		 
		switch (type)
		{
		case BEGIN_REQUEST:
			callback_function = &function_begin_request;
			break;
		case SEND_RESPONSE:
			callback_function = &function_send_response;
			break;
		case PRE_BEGIN_REQUEST:
			callback_function = &function_pre_begin_request;
			break;
		} 

		////////////////////////////////////////////////
		
		if (!callback_function || callback_function->IsEmpty()) 
			return 0 /* CONTINUE */;

		////////////////////////////////////////////////

		// Setup our lockers, isolate scope, and handle scope...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));
		
		////////////////////////////////////////////////
		 
		// Clone our arguments to be given to JavaScript.
		auto http_response_object = global_http_response_object.Get(isolate)->Clone();
		auto http_request_object = global_http_request_object.Get(isolate)->Clone();

		// Set the internal pointers in the objects.
		http_response_object->SetAlignedPointerInInternalField(0, pHttpContext);
		http_request_object->SetAlignedPointerInInternalField(0, pHttpContext);

		//////////////////////////////////////////////// 

		auto local_function = callback_function->Get(isolate);

		// Our argument count.
		auto argument_count = 2;

		// Our arguments to pass to the callback.
		v8::Local<v8::Value> arguments[3];
		arguments[0] = http_response_object;
		arguments[1] = http_request_object;

		////////////////////////////////////////////////

		// We want to add a third flag for our send response callback.
		if (type == SEND_RESPONSE)
		{
			// Update our argument count.
			argument_count = 3;

			// Set our third argument to be the provider flags.
			arguments[2] = v8pp::to_v8(isolate, ((ISendResponseProvider*)pObject)->GetFlags());
		}

		////////////////////////////////////////////////
		 
		auto result = local_function->Call(
			isolate->GetCurrentContext(),
			v8::Null(isolate),
			argument_count,
			arguments
		);

		// Check if our function returned anything...
		if (result.IsEmpty())
		{
			// Reset internal pointers.
			RESET_INTERNAL_POINTERS

			return 0 /* CONTINUE */;
		}

		////////////////////////////////////////////////

		// Get our result value.
		auto result_value = result.ToLocalChecked();

		////////////////////////////////////////////////
		
		if (type == PRE_BEGIN_REQUEST && !result_value->IsNumber())
		{
			v8pp::throw_ex(isolate, "The PRE_BEGIN_REQUEST callback must return either CONTINUE or FINISH.");

			////////////////////////////////////////////////

			// Reset internal pointers.
			RESET_INTERNAL_POINTERS

			////////////////////////////////////////////////

			return 0 /* CONTINUE */;
		}

		////////////////////////////////////////////////

		if (result_value->IsPromise())
		{	
			// Get our promise object.
			auto promise = result_value.As<v8::Promise>();

			// Check if our promise is already fulfilled or rejected.
			if (promise->State() == v8::Promise::kFulfilled || promise->State() == v8::Promise::kRejected)
			{
				// Reset internal pointers.
				RESET_INTERNAL_POINTERS

				////////////////////////////////////////////////

				// Cast our value to a request notification...
				return REQUEST_NOTIFICATION_STATUS(
					v8pp::from_v8<int>(isolate, promise->Result(), 0) 
						? RQ_NOTIFICATION_FINISH_REQUEST : RQ_NOTIFICATION_CONTINUE
				);
			}  

			//////////////////////////////////////////////////////

			// Our callback returned from the promise.
			auto callback = [](const v8::FunctionCallbackInfo<v8::Value>& args)
			{
				// Set our default notification status.
				int request_notification_status = v8pp::from_v8<int>(isolate, args[0], 0)
					? RQ_NOTIFICATION_FINISH_REQUEST : RQ_NOTIFICATION_CONTINUE;

#ifndef DISABLE_INTERNAL_POINTER_RESET
				// Cast our passthrough objects as an array.
				auto passthrough_objects = args.Data().As<v8::Array>();

				// Get the context.
				auto context = args.GetIsolate()->GetCurrentContext(); 

				// Fetch our response objects.
				auto http_response_object = passthrough_objects->Get(context, 0).ToLocalChecked().As<v8::Object>();
				auto http_request_object = passthrough_objects->Get(context, 1).ToLocalChecked().As<v8::Object>();

				// Cast our given Data,
				auto http_context = (IHttpContext*)(
					http_response_object->GetAlignedPointerFromInternalField(0)
				);

				// Regardless of any result,
				// we need to indicate that the we've completed
				// our execution to IIS.
				http_context->IndicateCompletion(
					REQUEST_NOTIFICATION_STATUS(request_notification_status)
				);

				// Reset internal pointers.
				RESET_INTERNAL_POINTERS

#else
				// Cast our given Data,
				auto http_context = (IHttpContext*)args.Data().As<v8::External>()->Value();

				// Regardless of any result,
				// we need to indicate that the we've completed
				// our execution to IIS.
				http_context->IndicateCompletion(
					REQUEST_NOTIFICATION_STATUS(request_notification_status)
				);
#endif 
			}; 

			////////////////////////////////////////////////

#ifndef DISABLE_INTERNAL_POINTER_RESET
			// Get our current context so we can create our array object.
			auto context = isolate->GetCurrentContext();

			// Pass our objects through.
			auto passthrough_objects = v8::Array::New(isolate, 2);
			passthrough_objects->Set(context, 0, http_response_object);
			passthrough_objects->Set(context, 1, http_request_object);

			// Create our callback function.
			auto function = v8::Function::New(
				isolate->GetCurrentContext(),
				callback,
				passthrough_objects
			).ToLocalChecked();
#else
			auto function = v8::Function::New(
				isolate->GetCurrentContext(),
				callback,
				v8::External::New(isolate, pHttpContext)
			).ToLocalChecked();
#endif
			
			// Attach our callback function to our promise to handle both scenarios.
			promise->Then(isolate->GetCurrentContext(), function, function);

			////////////////////////////////////////////////

			return RQ_NOTIFICATION_PENDING;
		}

		///////////////////////////////////////////

		// Reset internal pointers.
		RESET_INTERNAL_POINTERS

		///////////////////////////////////////////

		/*
		 * We need to normalize our return value.
		 *
		 * The PRE_BEGIN_REQUEST callback can only return GL_NOTIFICATION_HANDLED or GL_NOTIFICATION_CONTINUE,
		 * but the other callbacks must return RQ_NOTIFICATION_FINISH_REQUEST or RQ_NOTIFICATION_CONTINUE.
		 *
		 * They represent the same action but differ by value.
		 */
		auto return_int_value = v8pp::from_v8<int>(isolate, result_value, 0) ? 
			(type == PRE_BEGIN_REQUEST ? GL_NOTIFICATION_HANDLED : RQ_NOTIFICATION_FINISH_REQUEST)
			: RQ_NOTIFICATION_CONTINUE;

		///////////////////////////////////////////
		
		return return_int_value;
	}

	/**
	 * Converts a PSOCKADDR to a formatted string,
	 * works for both IPv4 and IPv6.
	 */
	std::string sock_to_ip(PSOCKADDR address)
	{
		if (!address)
		{
			v8pp::throw_ex(isolate, "invalid address for sock_to_ip");
			
			return std::string();
		}

		if (address->sa_family == AF_INET)
		{
			char ip_address[INET_ADDRSTRLEN] = { 0 };
			auto socket = (sockaddr_in*)address;

			InetNtopA(socket->sin_family, &socket->sin_addr, ip_address, sizeof ip_address);

			return std::string(ip_address);
		}
		
		if (address->sa_family == AF_INET6)
		{
			char ip_address[INET6_ADDRSTRLEN] = { 0 };
			auto socket = (sockaddr_in6*)address;

			InetNtopA(socket->sin6_family, &socket->sin6_addr, ip_address, sizeof ip_address);

			return std::string(ip_address);
		}

		v8pp::throw_ex(isolate, "invalid family for sock_to_ip");
		return std::string();
	}

	/**
	 * Executes a string containing JavaScript.
	 */
	bool execute_string(char * str, bool print_result, bool report_exceptions)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		// Enter the execution environment before evaluating any code.
		v8::Local<v8::String> name(
			v8::String::NewFromUtf8(
				isolate, "(shell)", 
				v8::NewStringType::kNormal
			).ToLocalChecked()
		);

		// Setup other related...
		v8::TryCatch try_catch(isolate);
		v8::ScriptOrigin origin(name);
		v8::Local<v8::Context> context(isolate->GetCurrentContext());
		v8::Local<v8::Script> script;

		// Setup our source...
		auto source = v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal).ToLocalChecked();

		// Compile.
		if (!v8::Script::Compile(context, source, &origin).ToLocal(&script))
		{
			// Print errors that happened during compilation.
			if (report_exceptions) report_exception(&try_catch);

			// Return false here...
			return false;
		}

		//////////////////////////////////////////////////

		v8::Local<v8::Value> result;

		if (!script->Run(context).ToLocal(&result))
		{
			assert(try_catch.HasCaught());

			// Print errors that happened during execution.
			if (report_exceptions) report_exception(&try_catch);

			return true;
		}

		//////////////////////////////////////////////////

		assert(!try_catch.HasCaught());
		if (print_result && !result->IsUndefined()) {
			// If all went well and the result wasn't undefined then print
			// the returned value.
			v8::String::Utf8Value str(isolate, result);
			const char* cstr = c_string(str);

			vs_printf("%s\n", cstr);
		}

		return true;
	}

	/**
	 * Executes a file by reading it's contents and 
	 * passing it to execute_string.
	 */
	void execute_file(const wchar_t * name)
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		FILE* file = _wfopen(name, L"rb");
		if (file == nullptr) return;

		fseek(file, 0, SEEK_END);
		size_t size = ftell(file);
		rewind(file);

		char* chars = new char[size + 1];
		chars[size] = '\0';
		for (size_t i = 0; i < size;) {
			i += fread(&chars[i], 1, size - i, file);
			if (ferror(file)) {
				fclose(file);
				return;
			}
		}
		fclose(file);

		if (!execute_string(chars, true, true)) {
			isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Error executing file",
					v8::NewStringType::kNormal).ToLocalChecked());
		}

		delete[] chars;

		vs_printf("Executed %ws script...\n", name);
		fflush(stdout);
	}

	/**
	 * Formats and reports an exception.
	 */
	void report_exception(v8::TryCatch * try_catch)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		v8::String::Utf8Value exception(isolate, try_catch->Exception());
		const char* exception_string = c_string(exception);
		v8::Local<v8::Message> message = try_catch->Message();

		if (message.IsEmpty())
		{
			vs_printf("%s\n", exception_string);
		}
		else {
			v8::Local<v8::Context> context(isolate->GetCurrentContext());
			int linenum = message->GetLineNumber(context).FromJust();

			vs_printf("%i: %s\n", linenum, exception_string);

			v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
			const char* sourceline_string = c_string(sourceline);
			vs_printf("%s\n", sourceline_string);

			int start = message->GetStartColumn(context).FromJust();
			for (int i = 0; i < start; i++) {
				vs_printf(" ");
			}
			int end = message->GetEndColumn(context).FromJust();
			for (int i = start; i < end; i++) {
				vs_printf("^");
			}
			vs_printf("\n");
			v8::Local<v8::Value> stack_trace_string;
			if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
				stack_trace_string->IsString() &&
				v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0) {
				v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
				const char* stack_trace_string = c_string(stack_trace);
				vs_printf("%s\n", stack_trace_string);
			}
		}
	}

	/**
	 * Converts a Utf8Value to a C string.
	 */
	const char* c_string(v8::String::Utf8Value& value)
	{
		return *value ? *value : "<string conversion failed>";
	}

	/**
	 * Uses OutputDebugStringA to print.
	 */
	int vs_printf(const char *format, ...)
	{
		char str[1024];

		va_list argptr;
		va_start(argptr, format);
		int ret = vsnprintf(str, sizeof(str), format, argptr);
		va_end(argptr);

		OutputDebugStringA(str);

		return ret;
	}
}