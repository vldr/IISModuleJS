#include "v8_wrapper.h"

namespace v8_wrapper
{
	namespace fs = std::experimental::filesystem;

	// All the global objects and functions for v8.
	v8::Global<v8::Object> global_db_object;
	v8::Global<v8::Object> global_fetch_object;

	v8::Global<v8::Object> global_http_response_object;
	v8::Global<v8::Object> global_http_request_object;

	v8::Global<v8::Object> global_ipc_object;
	 
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

	// A list containing all the loaded scripts to watch.
	std::vector<
		std::pair<
			std::experimental::filesystem::path, 
			fs::file_time_type
		>
	> loaded_scripts;

	// All variables needed for keeping track of the number of threads
	// launched, we wish to keep it below a certain threshold as to
	// not overload the machine.
	int thread_count = 0;
	std::condition_variable thread_count_cv;
	std::mutex thread_count_lock;

	/**
	 * The method that initializes everything necessary.
	 */
	void start(std::wstring app_pool_name)
	{
		// Setup our engine thread.
		std::thread engine_thread([app_pool_name] {	
			app_pool_folder_name = app_pool_name;

			script_name = L"Main.js";

			///////////////////////////
			 
			std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
			
			v8::V8::InitializePlatform(platform.get());
			v8::V8::InitializeICU(); 
			v8::V8::Initialize();
#ifdef _DEBUG
			v8::V8::SetFlagsFromString("--allow-natives-syntax --track-retaining-path --expose-gc");
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

		loaded_scripts.clear();

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
	*/
	std::experimental::filesystem::path& get_relative_file_path(std::wstring &raw_input)
	{
		static auto cached_paths = std::unordered_map<std::wstring, std::experimental::filesystem::path>();

		auto path = cached_paths.find(raw_input);

		if (path != cached_paths.end())
		{
			return path->second;
		}

		////////////////////////////////////////////

		std::wstring input = raw_input;

		// Windows backend protection (https://tools.ietf.org/html/rfc3986#section-7.3):
		auto pos = input.find(L"\\", 0);

		// Loop until there isn't any \ characters.
		while (pos != std::string::npos)
		{
			input.replace(pos, 1, L"/");
			pos = input.find(L"\\", 0);
		}

		////////////////////////////////////////////

		std::wstring output; 
		output.reserve(input.length());

		/**
		* RFC 3986 5.2.4.
		* https://tools.ietf.org/html/rfc3986#section-5.2.4
		*/

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
		cached_paths[raw_input] = fs_directory / fs::path(output);

		// Return the reference.
		return cached_paths[raw_input];
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
			path = path / app_pool_folder_name / fs::path(script).filename();
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
				}
			);

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
			
			return execute_string("(rpc)", (char*)script.c_str());
		});

		// Run our rpc server asynchronously.
		rpc_server.async_run();
#endif
		 
		//////////////////////////////////////////
		 
		auto script_path = get_path(script_name);

		// Add the root script with a default file time type so the file gets initially loaded.
		loaded_scripts.push_back(
			std::make_pair(
				script_path,
				fs::file_time_type()
			)
		);

		//////////////////////////////////////////
		 
		fs_directory = get_path() / app_pool_folder_name / "filesystem";

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

		// Wait for variable used for the find first change notification.
		DWORD wait_for = 0; 
		 
		// The error code used for checking the last write of each loaded script.
		std::error_code error_code;

		for (;;)
		{ 
			// Loop through all our loaded scripts.
			for (auto script = loaded_scripts.begin(); script != loaded_scripts.end(); script++)
			{
				// Check if one of the the scripts has been modified.
				if (script->second != fs::last_write_time(script->first, error_code) && !error_code)
				{
					// Reset the engine if so.
					reset_engine();

					// Reload the main script.
					execute_file(script_path);

					// Break out of the loop.
					break;
				}
			}
			 
			//////////////////////////////////////////
			 
			if (wait_for != WAIT_TIMEOUT) 
			{
				change_notify_handle = FindFirstChangeNotificationW(
					fs_directory.c_str(),
					TRUE,
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

				// Get the path to the file.
				auto path = get_path(name);

				// Execute the file in v8.
				execute_file(
					path
				);
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

		global.set_const("BEGIN_REQUEST", 0);
		global.set_const("SEND_RESPONSE", 1);
		global.set_const("PRE_BEGIN_REQUEST", 2);
		
		global.set_const("CONTINUE", 0);
		global.set_const("FINISH", 1);
		
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

			FetchRequest fetch_request(std::move(hostname), std::move(path));

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

			/////////////////////////////////////////////

			// Wait until threads free up.
			{
				auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
				thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

				thread_count++;
			} 

			/////////////////////////////////////////////

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

				////////////////////////////////////////////

				// Decrement out thread count.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count--;
				}

				// Notify all waitees.
				thread_count_cv.notify_one();
				
				////////////////////////////////////////////

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

		// ipc.init(name: String): IPCObject
		ipc_module.set("init", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for db.init");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for db.init");

			/////////////////////////////////////////////
			 
			auto name = v8pp::from_v8<std::string>(isolate, args[0]);
			
			/////////////////////////////////////////////
			
			auto ipc_context = std::make_unique<IPC_KV>(name.c_str());

			/////////////////////////////////////////////

			auto ipc_object = global_ipc_object.Get(isolate)->Clone();
			auto ipc_handler = new IPCHandler(isolate, ipc_object, ipc_context.release());

			//////////////////////////////////

			ipc_handler->ipc_object.SetWeak(
				ipc_handler,
				[](const v8::WeakCallbackInfo<IPCHandler>& data)
				{
					// Reset our JS object.
					data.GetParameter()->ipc_object.Reset();

					///////////////////////////////

					// Decrement our external memory usage.
					data.GetIsolate()->AdjustAmountOfExternalAllocatedMemory(
						-(int64_t)sizeof(IPC_KV)
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
				(int64_t)sizeof(IPC_KV)
			);

			//////////////////////////////////

			args.GetReturnValue().Set(
				ipc_object
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

		// fs.copy(existingFileName: String, newFileName: String, overwrite: boolean {optional, default: false}): boolean 
		fs_module.set("copy", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 2)
				throw std::exception("invalid function signature for fs.copy");

			if (!args[0]->IsString() || !args[1]->IsString())
				throw std::exception("invalid first two parameters, must be both a string for fs.copy");

			/////////////////////////////////////////////

			auto existing_file_name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[0]);
			auto existing_file_path = get_relative_file_path(existing_file_name);
			
			auto new_file_name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[1]);
			auto new_file_path = get_relative_file_path(new_file_name);

			auto overwrite = args.Length() > 2 && args[2]->IsBoolean() && v8pp::from_v8<bool>(args.GetIsolate(), args[2]);

			/////////////////////////////////////////////

			RETURN_THIS(
				(bool)CopyFileW(existing_file_path.c_str(), new_file_path.c_str(), !overwrite)
			)
		});

		// fs.exists(fileName: String): boolean 
		fs_module.set("exists", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for fs.exists");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for fs.exists");

			/////////////////////////////////////////////

			auto file_name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[0]);
			auto file_path = get_relative_file_path(file_name);

			/////////////////////////////////////////////

			RETURN_THIS(
				(bool)PathFileExistsW(file_path.c_str())
			)
		});

		// fs.delete(fileName: String): boolean 
		fs_module.set("delete", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for fs.delete");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for fs.delete");

			/////////////////////////////////////////////

			auto file_name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[0]);
			auto file_path = get_relative_file_path(file_name);

			/////////////////////////////////////////////

			RETURN_THIS(
				(bool)DeleteFileW(file_path.c_str())
			)
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

		db_module.set_const("STRING", 0);
		db_module.set_const("INTEGER", 1);
		db_module.set_const("DOUBLE", 2);
		db_module.set_const("BOOL", 3);

		////////////////////////////////////////

		// gzip Property  
		v8pp::module gzip_module(isolate);

		// gzip.compress(input: String, compressionLevel: Integer {32-bit only, optional, default: 6}): Promise<Uint8Array>
		gzip_module.set("compress", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for gzip.compress");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for gzip.compress");

			auto string = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			/////////////////////////////////////////////
			 
			// Setup our default workload.
			int compressionLevel = Z_DEFAULT_COMPRESSION;

			// Check if we were provided a custom workload value. 
			if (args.Length() > 1 && args[1]->IsInt32())
			{
				compressionLevel = std::max(
					0, std::min(v8pp::from_v8<int>(args.GetIsolate(), args[1]), 9)
				);
			}

			/////////////////////////////////////////////

			// Wait until threads free up.
			{
				auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
				thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

				thread_count++;
			}

			/////////////////////////////////////////////

			// Setup a resolver.
			auto resolver = v8::Promise::Resolver::New(
				args.GetIsolate()->GetCurrentContext()
			).ToLocalChecked();

			// Setup a global resolver object.
			auto resolver_global = v8::Global<v8::Promise::Resolver>(
				args.GetIsolate(), resolver
			);

			/////////////////////////////////////////////

			// Set the return value to our promise.
			args.GetReturnValue().Set(
				resolver_global.Get(isolate)->GetPromise()
			);

			std::thread gzip_thread([string = std::move(string), compressionLevel, resolver = std::move(resolver_global)] {
				// Setup an empty string because EXCEPTIONS! 
				std::string compressed;

				try 
				{
					compressed = gzip::compress(string.c_str(), string.length(), compressionLevel);
				}
				catch (std::exception & e) 
				{
					vs_printf("Exception at gzip.compress in gzip_thread (%s)\n", e.what());
				} 

				/////////////////////////////////////////////

				// Decrement out thread count.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count--;
				}
				 
				// Notify all waitees.
				thread_count_cv.notify_one();

				/////////////////////////////////////////////

				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Context::Scope context_scope(context.Get(isolate));

				if (!compressed.empty())
				{
					auto array_buffer = v8::ArrayBuffer::New(
						isolate,
						compressed.size()
					);

					////////////////////////////////////////////////
					
					std::memcpy(
						array_buffer->GetContents().Data(),
						compressed.data(),
						compressed.size()
					);

					////////////////////////////////////////////////

					auto uint8_array = v8::Uint8Array::New(
						array_buffer,
						0,
						compressed.size()
					); 

					////////////////////////////////////////////////

					resolver.Get(isolate)->Resolve(
						isolate->GetCurrentContext(),
						uint8_array
					);
				}
				else
				{
					resolver.Get(isolate)->Reject(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, "failed to compress using gzip.")
					);
				}			
			}); 

			gzip_thread.detach();
		}); 

		// gzip.decompress(input: Uint8Array): Promise<String>
		gzip_module.set("decompress", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for gzip.decompress");
			 
			if (!args[0]->IsUint8Array())
				throw std::exception("invalid first parameter, must be a uint8array for gzip.decompress");

			auto uint8array_contents = args[0].As<v8::Uint8Array>()->Buffer()->GetContents();

			// Get the uint8array buffer contents.
			auto buffer = uint8array_contents.Data();
			auto length = uint8array_contents.ByteLength();

			/////////////////////////////////////////////

			// Wait until threads free up.
			{
				auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
				thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

				thread_count++;
			} 

			/////////////////////////////////////////////

			// Setup a resolver.
			auto resolver = v8::Promise::Resolver::New(
				args.GetIsolate()->GetCurrentContext()
			).ToLocalChecked();

			// Setup a global resolver object.
			auto resolver_global = v8::Global<v8::Promise::Resolver>(
				args.GetIsolate(), resolver
			);

			/////////////////////////////////////////////

			// Set the return value to our promise.
			args.GetReturnValue().Set(
				resolver_global.Get(isolate)->GetPromise()
			);

			std::thread gzip_thread([buffer, length, resolver = std::move(resolver_global)] {
				std::string decompressed;

				try 
				{
					decompressed = gzip::decompress((const char*)buffer, length);
				}
				catch (std::exception & e) 
				{
					vs_printf("Exception at gzip.decompressed in gzip_thread (%s)\n", e.what());
				} 

				/////////////////////////////////////////////

				// Decrement out thread count.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count--;
				}
				 
				// Notify all waitees.
				thread_count_cv.notify_one();

				/////////////////////////////////////////////

				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Context::Scope context_scope(context.Get(isolate));
				 
				if (!decompressed.empty())
				{
					resolver.Get(isolate)->Resolve(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, std::move(decompressed))
					);
				}
				else
				{
					resolver.Get(isolate)->Reject(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, "failed to decompress using gzip.")
					);
				}			
			}); 

			gzip_thread.detach();
		});

		gzip_module.set_const("NO_COMPRESSION", 0);
		gzip_module.set_const("BEST_SPEED", 1);
		gzip_module.set_const("BEST_COMPRESSION", 9);

		////////////////////////////////////////

		// crypto Property  
		v8pp::module crypto_module(isolate);
		 
		// bcrypt Module
		v8pp::module bcrypt_module(isolate);

		// Add our bcrypt module to the crypto module.
		crypto_module.set_const("bcrypt", bcrypt_module);

		// crypto.bcrypt.hash(input: String, workload: Integer {32-bit only, optional, default: 12}): Promise<String>
		bcrypt_module.set("hash", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1)
				throw std::exception("invalid function signature for crypto.bcrypt");

			if (!args[0]->IsString())
				throw std::exception("invalid first parameter, must be a string for crypto.bcrypt");

			/////////////////////////////////////////////

			auto input = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			/////////////////////////////////////////////

			// Setup our default workload.
			int workload = 12;

			// Check if we were provided a custom workload value.
			if (args.Length() > 1 && args[1]->IsInt32())
				workload = v8pp::from_v8<int>(args.GetIsolate(), args[1]);

			/////////////////////////////////////////////

			// Wait until threads free up.
			{
				auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
				thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

				thread_count++;
			}

			/////////////////////////////////////////////

			// Setup a resolver.
			auto resolver = v8::Promise::Resolver::New(
				args.GetIsolate()->GetCurrentContext()
			).ToLocalChecked();

			// Setup a global resolver object.
			auto resolver_global = v8::Global<v8::Promise::Resolver>(
				args.GetIsolate(), resolver
			);

			/////////////////////////////////////////////

			// Set the return value to our promise.
			args.GetReturnValue().Set(
				resolver_global.Get(isolate)->GetPromise()
			);

			std::thread bcrypt_thread([workload, input_value = std::move(input), resolver = std::move(resolver_global)] {
				char salt[BCRYPT_HASHSIZE];
				char hash[BCRYPT_HASHSIZE];

				// Attempt to generate a salt.
				int result = bcrypt_gensalt(workload, salt);

				// Check if we were unable to generate the salt.
				if (result != 0)
				{
					goto finish;
				}

				/////////////////////////////////////////////

				// Attempt to hash the password.
				result = bcrypt_hashpw(input_value.c_str(), salt, hash);

				// Check if we were unable to generate the hash.
				if (result != 0)
				{
					goto finish;
				}

				/////////////////////////////////////////////

			finish:

				// Decrement out thread count.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count--;
				}
				 
				// Notify all waitees.
				thread_count_cv.notify_one();

				/////////////////////////////////////////////

				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Context::Scope context_scope(context.Get(isolate));

				if (result != 0)
				{
					resolver.Get(isolate)->Reject(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, "failed to generate bcrypt hash.")
					);
				}
				else
				{
					// Resolve our promise.
					resolver.Get(isolate)->Resolve(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, hash, strlen(hash))
					);
				}
			}); 

			bcrypt_thread.detach();
		});

		// crypto.bcrypt.check(password: String, hash: String): Promise<bool>
		bcrypt_module.set("check", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 2)
				throw std::exception("invalid function signature for crypto.bcryptCompare");

			if (!args[0]->IsString() || !args[1]->IsString())
				throw std::exception("invalid first parameter, must be a string for crypto.bcryptCompare");

			/////////////////////////////////////////////

			auto password = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
			auto hash = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);

			/////////////////////////////////////////////

			// Wait until threads free up.
			{
				auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
				thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

				thread_count++;
			}

			/////////////////////////////////////////////

			// Setup a resolver.
			auto resolver = v8::Promise::Resolver::New(
				args.GetIsolate()->GetCurrentContext()
			).ToLocalChecked();

			// Setup a global resolver object.
			auto resolver_global = v8::Global<v8::Promise::Resolver>(
				args.GetIsolate(), resolver
			);

			/////////////////////////////////////////////

			// Set the return value to our promise.
			args.GetReturnValue().Set(
				resolver_global.Get(isolate)->GetPromise()
			);

			std::thread bcrypt_thread([
				input_password = std::move(password),
				input_hash = std::move(hash),
				resolver = std::move(resolver_global)
			]  
			{
				bool result = (bcrypt_checkpw(input_password.c_str(), input_hash.c_str()) == 0);

				////////////////////////////////////////////

				// Decrement out thread count.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count--;
				}

				// Notify all waitees.
				thread_count_cv.notify_one();

				/////////////////////////////////////////////

				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Context::Scope context_scope(context.Get(isolate));

				// Resolve our promise.
				resolver.Get(isolate)->Resolve(
					isolate->GetCurrentContext(),
					v8pp::to_v8(isolate, result)
				);
			}); 

			bcrypt_thread.detach();
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
		
		// crypto Object
		global.set_const("crypto", crypto_module);
		
		// gzip Object
		global.set_const("gzip", gzip_module);

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
		//      IPC JS Object      //
		/////////////////////////////
		if (global_ipc_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate); 

			// Setup our functions

			// ipc.set(key: String, value: any): void
			module.set("set", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!IPC_OBJECT)
					throw std::exception("invalid function pointer for ipc.set");

				if (args.Length() < 2) 
					throw std::exception("invalid function signature for ipc.set");
			
				if (!args[0]->IsString()) 
					throw std::exception("invalid first parameter, must be a string for ipc.set");
				 
				/////////////////////////////////////////////

				auto key = v8pp::from_v8<std::string>(isolate, args[0]);

				/////////////////////////////////////////////

				SerializerDelegate serializer_delegate(isolate);
				v8::ValueSerializer serializer(isolate, &serializer_delegate);

				auto result = serializer.WriteValue(
					isolate->GetCurrentContext(),
					args[1]
				).FromMaybe(false);

				if (!result) throw std::exception("invalid object given, unable to serialize for ipc.set");

				/////////////////////////////////////////////

				std::pair<uint8_t*, size_t> buffer = serializer.Release();

				IPC_OBJECT->set(key, buffer.first, buffer.second);

				serializer_delegate.FreeBufferMemory(buffer.first);
			});

			// ipc.get(key: String): any || null
			module.set("get", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!IPC_OBJECT)
					throw std::exception("invalid function pointer for ipc.get");
			
				if (args.Length() < 1) 
					throw std::exception("invalid function signature for ipc.get");

				if (!args[0]->IsString()) 
					throw std::exception("invalid first parameter, must be a string for ipc.get");

				auto key = v8pp::from_v8<std::string>(isolate, args[0]);

				/////////////////////////////////////////////

				unsigned char buffer[IPCKV_DATA_SIZE];
				size_t size;

				/////////////////////////////////////////////

				bool result = IPC_OBJECT->get(key, buffer, size);

				if (!result) RETURN_NULL

				/////////////////////////////////////////////

				DeserializerDelegate deserializer_delegate(isolate);
				v8::ValueDeserializer deserializer(
					isolate,
					buffer,
					size,
					&deserializer_delegate
				); 

				auto value = deserializer.ReadValue(isolate->GetCurrentContext());

				if (value.IsEmpty()) throw std::exception("unable to deserialize value for ipc.get");

				/////////////////////////////////////////////

				args.GetReturnValue().Set(
					value.ToLocalChecked()
				); 
			});

			// ipc.close(): void
			module.set("close", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!IPC_OBJECT)
					throw std::exception("invalid function pointer for ipc.close");

				IPC_OBJECT->close();

				args.This()->SetAlignedPointerInInternalField(0, nullptr);
			});

			// Set our internal field count.
			module.obj_->SetInternalFieldCount(1);

			// Reset our pointer...
			global_ipc_object.Reset(isolate, module.new_instance());
		}


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

			// exec(): Promise<void>
			module.set("exec", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for exec");

				/////////////////////////////////////////////

				// Wait until threads free up.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

					thread_count++; 
				}

				/////////////////////////////////////////////

				// Setup a resolver.
				auto resolver = v8::Promise::Resolver::New(
					args.GetIsolate()->GetCurrentContext()
				).ToLocalChecked();

				// Setup a global resolver object.
				auto resolver_global = v8::Global<v8::Promise::Resolver>(
					args.GetIsolate(), resolver
				);

				/////////////////////////////////////////////

				// Set the return value to our promise.
				args.GetReturnValue().Set(
					resolver_global.Get(isolate)->GetPromise()
				);

				std::thread db_thread([db_context = DB_CONTEXT, resolver = std::move(resolver_global)] {
					std::string error_message;

					try 
					{
						db_context->statement.exec();
					} 
					catch (std::exception const &e) 
					{
						error_message = e.what();
					}

					/////////////////////////////////////////////

					// Decrement out thread count.
					{
						auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
						thread_count--;
					} 

					// Notify all waitees.
					thread_count_cv.notify_one();

					/////////////////////////////////////////////

					v8::Locker locker(isolate);
					v8::Isolate::Scope isolate_scope(isolate);
					v8::HandleScope handle_scope(isolate);
					v8::Context::Scope context_scope(context.Get(isolate));

					if (error_message.empty())
					{
						resolver.Get(isolate)->Resolve(
							isolate->GetCurrentContext(),
							v8::Undefined(isolate)
						);
					}
					else
					{
						resolver.Get(isolate)->Reject(
							isolate->GetCurrentContext(),
							v8pp::to_v8(isolate, error_message)
						);
					}
				});

				db_thread.detach();
			});

			// execSync(): void
			module.set("execSync", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for exec");

				/////////////////////////////////////////////

				DB_CONTEXT->statement.exec();
			});  

			// query(): Promise<void>
			module.set("query", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for query");

				/////////////////////////////////////////////

				// Wait until threads free up.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

					thread_count++;
				}

				/////////////////////////////////////////////

				// Setup a resolver.
				auto resolver = v8::Promise::Resolver::New(
					args.GetIsolate()->GetCurrentContext()
				).ToLocalChecked();

				// Setup a global resolver object.
				auto resolver_global = v8::Global<v8::Promise::Resolver>(
					args.GetIsolate(), resolver
				);

				/////////////////////////////////////////////

				// Set the return value to our promise.
				args.GetReturnValue().Set(
					resolver_global.Get(isolate)->GetPromise()
				);

				std::thread db_thread([db_context = DB_CONTEXT, resolver = std::move(resolver_global)] {
					std::string error_message;

					try 
					{
						db_context->result = db_context->statement.query();
					} 
					catch (std::exception const &e) 
					{
						error_message = e.what();
					}

					/////////////////////////////////////////////

					// Decrement out thread count.
					{
						auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
						thread_count--;
					}

					// Notify all waitees.
					thread_count_cv.notify_one();

					/////////////////////////////////////////////

					v8::Locker locker(isolate);
					v8::Isolate::Scope isolate_scope(isolate);
					v8::HandleScope handle_scope(isolate);
					v8::Context::Scope context_scope(context.Get(isolate));

					if (error_message.empty())
					{
						resolver.Get(isolate)->Resolve(
							isolate->GetCurrentContext(),
							v8::Undefined(isolate)
						);
					}
					else
					{
						resolver.Get(isolate)->Reject(
							isolate->GetCurrentContext(),
							v8pp::to_v8(isolate, error_message)
						);
					}
				});

				db_thread.detach();
			}); 

			// querySync(): void
			module.set("querySync", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for query");
				 
				/////////////////////////////////////////////

				DB_CONTEXT->result = DB_CONTEXT->statement.query();
			}); 

			// queryRow(): Promise<boolean>
			module.set("queryRow", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for row");

				/////////////////////////////////////////////

				// Wait until threads free up.
				{
					auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
					thread_count_cv.wait(unique_lock, []() { return thread_count < MAX_THREADS; });

					thread_count++;
				}

				/////////////////////////////////////////////

				// Setup a resolver.
				auto resolver = v8::Promise::Resolver::New(
					args.GetIsolate()->GetCurrentContext()
				).ToLocalChecked();

				// Setup a global resolver object.
				auto resolver_global = v8::Global<v8::Promise::Resolver>(
					args.GetIsolate(), resolver
				);

				/////////////////////////////////////////////

				// Set the return value to our promise.
				args.GetReturnValue().Set(
					resolver_global.Get(isolate)->GetPromise()
				);

				std::thread db_thread([db_context = DB_CONTEXT, resolver = std::move(resolver_global)] {
					std::string error_message;

					try 
					{
						db_context->result = db_context->statement.row();
					} 
					catch (std::exception const &e) 
					{
						error_message = e.what();
					}

					/////////////////////////////////////////////

					// Decrement out thread count.
					{
						auto unique_lock = std::unique_lock<std::mutex>(thread_count_lock);
						thread_count--;
					}

					// Notify all waitees.
					thread_count_cv.notify_one();

					/////////////////////////////////////////////

					v8::Locker locker(isolate);
					v8::Isolate::Scope isolate_scope(isolate);
					v8::HandleScope handle_scope(isolate);
					v8::Context::Scope context_scope(context.Get(isolate));

					if (error_message.empty())
					{
						resolver.Get(isolate)->Resolve(
							isolate->GetCurrentContext(),
							v8pp::to_v8(isolate, !db_context->result.empty())
						);
					}
					else
					{
						resolver.Get(isolate)->Reject(
							isolate->GetCurrentContext(),
							v8pp::to_v8(isolate, error_message)
						);
					}
				});

				db_thread.detach();
			});
			
			// queryRowSync(): boolean
			module.set("queryRowSync", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!DB_CONTEXT) throw std::exception("invalid db context for row");

				/////////////////////////////////////////////

				DB_CONTEXT->result = DB_CONTEXT->statement.row();
				
				RETURN_THIS(
					!DB_CONTEXT->result.empty()
				)
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
						: DB_CONTEXT->result.get<std::string>(std::move(input_col_name))
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

					if (with_index) DB_CONTEXT->statement.bind(index, std::move(value));
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(std::move(value));
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

					if (with_index) DB_CONTEXT->statement.bind(index, std::move(value));
					else DB_CONTEXT->statement = DB_CONTEXT->statement.bind(std::move(value));
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

				auto method = HTTP_REQUEST->GetHttpMethod();

				args.GetReturnValue().Set(
					v8pp::to_v8(isolate, method, strlen(method))
				);
			});

			// getAbsPath(): String
			module.set("getAbsPath", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getMethod");

				args.GetReturnValue().Set(
					v8pp::to_v8(
						isolate,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pAbsPath,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.AbsPathLength / sizeof(wchar_t)
					)
				);
			});
			 
			// getFullUrl(): String
			module.set("getFullUrl", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getFullUrl");

				args.GetReturnValue().Set(
					v8pp::to_v8(
						isolate,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pFullUrl,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.FullUrlLength / sizeof(wchar_t)
					)
				);
			}); 

			// getQueryString(): String
			module.set("getQueryString", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getQueryString");

				args.GetReturnValue().Set(
					v8pp::to_v8(
						isolate,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pQueryString,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.QueryStringLength / sizeof(wchar_t)
					)
				);
			});

			// getPath(): String
			module.set("getPath", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getQueryString");

				args.GetReturnValue().Set(
					v8pp::to_v8(
						isolate,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pAbsPath,
						(HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.AbsPathLength
						+ HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.QueryStringLength)
						/ sizeof(wchar_t)
					)
				);
			});

			// getHost(): String
			module.set("getHost", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_CONTEXT || !HTTP_REQUEST) throw std::exception("invalid p_http_request for getHost");

				args.GetReturnValue().Set(
					v8pp::to_v8(
						isolate,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pHost,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.HostLength / sizeof(wchar_t)
					)
				);
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
				int request_notification_status = v8pp::from_v8<int>(args.GetIsolate(), args[0], 0)
					? RQ_NOTIFICATION_FINISH_REQUEST : RQ_NOTIFICATION_CONTINUE;

#ifndef DISABLE_INTERNAL_POINTER_RESET
				// Cast our passthrough objects as an array.
				auto passthrough_object = (PassthroughObject*)(args.Data().As<v8::External>()->Value());

				// Setup our objects.
				auto http_response_object = passthrough_object->m_http_request_object.Get(args.GetIsolate());
				auto http_request_object = passthrough_object->m_http_response_object.Get(args.GetIsolate());

				// Cast our given Data,
				auto http_context = (IHttpContext*)(
					passthrough_object->m_http_context
					);

				// Regardless of any result,
				// we need to indicate that the we've completed
				// our execution to IIS.
				http_context->IndicateCompletion(
					REQUEST_NOTIFICATION_STATUS(request_notification_status)
				);

				// Reset internal pointers.
				RESET_INTERNAL_POINTERS

				// Delete the object.
				delete passthrough_object;
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
			auto objects = new PassthroughObject(
				isolate,
				pHttpContext,
				std::move(http_response_object),
				std::move(http_request_object)
			);

			auto function = v8::Function::New(
				isolate->GetCurrentContext(),
				callback,
				v8::External::New(isolate, objects)
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
	bool execute_string(const char * script_name, char * str)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		// Enter the execution environment before evaluating any code.
		v8::Local<v8::String> name(
			v8::String::NewFromUtf8(
				isolate, script_name,
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
			report_exception(&try_catch);

			// Return false here...
			return false;
		}

		//////////////////////////////////////////////////

		v8::Local<v8::Value> result;

		if (!script->Run(context).ToLocal(&result))
		{
			assert(try_catch.HasCaught());

			// Print errors that happened during execution.
			report_exception(&try_catch);

			return true;
		}

		//////////////////////////////////////////////////

		assert(!try_catch.HasCaught());

		return true;
	}

	/**
	 * Executes a file by reading it's contents and 
	 * passing it to execute_string.
	 */
	void execute_file(std::experimental::filesystem::path & script_path)
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		/////////////////////////////////////////////

		// Push our script to the loaded scripts
		loaded_scripts.push_back( 
			std::make_pair(
				script_path,
				fs::last_write_time(script_path)
			)
		);

		/////////////////////////////////////////////

		// Attempt to open a file handle.
		auto file = _wfopen(script_path.c_str(), L"rb");

		// Check if we were successful in opening a file handle.
		if (file == nullptr) 
		{
			// Throw a javascript exception.
			isolate->ThrowException(
				v8::String::NewFromUtf8(isolate, "failed to open a handle to the script file", v8::NewStringType::kNormal)
					.ToLocalChecked()
			);
			 
			// Return here.
			return;
		} 

		/////////////////////////////////////////////

		// Check the file size.
		fseek(file, 0, SEEK_END);
		size_t size = ftell(file); 
		rewind(file); 

		/////////////////////////////////////////////

		// Create a unqiue pointer to our char array.
		auto chars_unique_ptr = std::make_unique<char[]>(size + 1);

		// Get the raw pointer to the chars.
		auto chars = chars_unique_ptr.get();

		// Place a terminating byte at the end;
		chars[size] = 0;

		// Attempt to read the entire file into our chars array.
		fread(chars, sizeof(char), size, file);

		// Check if we were successful in reading our file.
		if (ferror(file))
		{
			isolate->ThrowException(
				v8::String::NewFromUtf8(isolate, "failed to read the contents of the script file", v8::NewStringType::kNormal)
				.ToLocalChecked()
			);

			// Close our file handle.
			fclose(file);

			return;
		}

		// Close our file handle.
		fclose(file);

		// Attempt to execute our script.
		if (!execute_string(script_path.filename().u8string().c_str(), chars))
		{
			isolate->ThrowException(
				v8::String::NewFromUtf8(isolate, "failed to execute script file", v8::NewStringType::kNormal)
					.ToLocalChecked()
			);

			return;
		}

		// Inform the user that we've loaded our script successfully.
		vs_printf("Loaded %ws script...\n", script_path.filename().c_str());
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