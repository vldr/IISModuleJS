#include "v8_wrapper.h"
#include <simdb/simdb.hpp>

namespace v8_wrapper
{
	namespace fs = std::experimental::filesystem;

	// All the global objects and functions for v8.
	v8::Global<v8::Object> global_http_response_object;
	v8::Global<v8::Object> global_http_request_object;

	/////////////////////////////////////////////////

	v8::Global<v8::Function> function_pre_begin_request;
	v8::Global<v8::Function> function_begin_request;
	v8::Global<v8::Function> function_send_response;

	// A persistent context for v8.
	v8::Persistent<v8::Context> context;

	// The unique isolate for v8.
	v8::Isolate * isolate = nullptr; 

	// The name of the default script to be launched. 
	std::wstring script_name;

	// Simdb database use for ipc.get and ipc.set
	simdb db;

	bool asyncCompletionPending = false;

	/**
	 * The method that initializes everything necessary.
	 */
	void start(std::wstring app_pool_name)
	{ 
		// Setup our engine thread.
		std::thread engine_thread([app_pool_name] {	
			// Setup our db.
			db = simdb("IISModule", 1024, 4096);
			db.flush();

			// Set our app pool name up.
			script_name = app_pool_name;
			script_name.append(L".js");
			 
			// Initialize...
			std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
			
			v8::V8::InitializePlatform(platform.get());
			v8::V8::InitializeICU();
			v8::V8::Initialize();
#ifdef _DEBUG
			v8::V8::SetFlagsFromString("--always-opt --allow-natives-syntax --trace-opt");
#else
			v8::V8::SetFlagsFromString("--always-opt"); 
#endif
			// Setup create params...
			v8::Isolate::CreateParams create_params;

			create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

			// Setup our isolate...
			isolate = v8::Isolate::New(create_params);

			//////////////////////////////////////////

			// Load our script and watch them.
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
		function_send_response.Reset();
		function_pre_begin_request.Reset();

		// Reset our context...
		context.Reset(isolate, create_shell_context());

		// Initialize our objects...
		initialize_objects();
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
			return execute_string((char*)script.c_str(), true, true);
		});

		// Run our rpc server asynchronously.
		rpc_server.async_run();
#endif

		//////////////////////////////////////////

		// Get our working directory path to monitor for changes.
		auto script_path = get_path(script_name);

		//////////////////////////////////////////

		// Setup our last write.
		fs::file_time_type last_write;
		std::error_code ec;

		for (;;)
		{
			// Check if our file exists, and last write has changed
			if (last_write != fs::last_write_time(script_path, ec) && !ec)
			{
				// Update our last write.
				last_write = fs::last_write_time(script_path);

				// Log our action.
				vs_printf("Resetting engine and loading \"%ws\" script...\n", script_name.c_str());

				// Reset our engine by creating a new contex.
				reset_engine();

				// Execute our file using the script path.
				execute_file(script_path.c_str());
			}

			// Sleep for one second.
			std::this_thread::sleep_for(std::chrono::seconds(1));
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
		//	   isSSL: bool,
		//	   method: String {optional, GET}, 
		//     params: Object<String, String> {optional}
		// ): Promise
		http_module.set("fetch", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			// Check argument length.
			if (args.Length() < 3) throw std::exception("invalid function signature for http.fetch");

			// Arguments pertaining to fetch.
			auto hostname = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
			auto path = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);
			auto is_ssl = v8pp::from_v8<bool>(args.GetIsolate(), args[2]);
			auto method = v8pp::from_v8<std::string>(args.GetIsolate(), args[3], "GET");

			// Setup our params if we we're given them.
			httplib::Params params; 

			// Check if we were given our fifth argument.
			if (args.Length() >= 5 && args[4]->IsObject())
			{
				auto context = args.GetIsolate()->GetCurrentContext();
				auto object = args[4].As<v8::Object>();
				auto prop_names = object->GetPropertyNames(context).ToLocalChecked();

				for (uint32_t i = 0, count = prop_names->Length(); i < count; ++i)
				{
					v8::Local<v8::Value> key = prop_names->Get(context, i).ToLocalChecked();
					v8::Local<v8::Value> val = object->Get(context, key).ToLocalChecked();

					params.emplace(
						v8pp::from_v8<std::string>(isolate, key), 
						v8pp::from_v8<std::string>(isolate, val)
					);
				}
			}

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
				hostname, 
				path,
				is_ssl,
				method,
				params
			] {
				std::shared_ptr<httplib::Response> response;
				 
				// Use httplib::SSLClient if our endpoint is secure socket.
				if (is_ssl)
				{
					httplib::SSLClient client(hostname);
					client.enable_server_certificate_verification(false);

					if (method == "GET") response = client.Get(path.c_str());
					else if (method == "POST") response = client.Post(path.c_str(), params);
				}
				// Otherwise, use httplib::Client.
				else
				{
					httplib::Client client(hostname);

					if (method == "GET") response = client.Get(path.c_str());
					else if (method == "POST") response = client.Post(path.c_str(), params);
				}

				//////////////////////////////////////////
				
				// We should only lock once all of
				// the thread-blocking functions have finished.
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

				// Create a response module.
				v8pp::module response_module(isolate);
				response_module.set_const("body", response->body);
				response_module.set_const("status", response->status);

				// Resolve our request.
				resolver.Get(isolate)->Resolve(
					isolate->GetCurrentContext(), 
					response_module.new_instance()
				);
			});

			request_thread.detach();
		});

		////////////////////////////////////////

		// ipc Property
		v8pp::module ipc_module(isolate);

		// ipc.set(key: String, value: any): void
		ipc_module.set("set", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			// Check if atleast one argument was provided.
			if (args.Length() < 2) throw std::exception("invalid function signature for ipc.set");

			// Check if our first parameter is a string.
			if (!args[0]->IsString()) throw std::exception("invalid first parameter, must be a string for ipc.set");

			/////////////////////////////////////////////

			// Stringify our second object.
			auto key = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
			 
			/////////////////////////////////////////////

			// Setup our serializer delegate to handle all our data.
			SerializerDelegate serializer_delegate(args.GetIsolate());

			// Setup our value serializer that will actually perform the serialization.
			v8::ValueSerializer serializer(args.GetIsolate(), &serializer_delegate);

			// Write our value.
			auto result = serializer.WriteValue(
				args.GetIsolate()->GetCurrentContext(),
				args[1]
			).FromMaybe(false);

			// Check if we were able to successfully write our value to the serializer.
			if (!result) throw std::exception("invalid object given, unable to serialize for ipc.set");

			// Release the buffer.
			std::pair<uint8_t*, size_t> buffer = serializer.Release();

			// Place the value in the key-value store.
			result = db.put(key.data(), key.length(), buffer.first, buffer.second);
			
			// Free the buffer after we've placed it into our key-value store.
			serializer_delegate.FreeBufferMemory(buffer.first);
		});

		// ipc.get(key: String): any || null
		ipc_module.set("get", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			// Need this to return our custom object.
			v8::EscapableHandleScope escapable_handle_scope(args.GetIsolate()); 
			
			// Check if atleast one argument was provided.
			if (args.Length() < 1) throw std::exception("invalid function signature for ipc.get");

			// Check if our first parameter is a string.
			if (!args[0]->IsString()) throw std::exception("invalid first parameter, must be a string for ipc.get");

			// Attempt to get our key. 
			auto key = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			/////////////////////////////////////////////

			// Length of the buffer to create..
			simdb::u32 len = 0;

			// Get the length of the object.
			db.len(key, &len);

			// Check if our length is not zero.
			if (!len) RETURN_NULL

			/////////////////////////////////////////////

			// Allocate our buffer...
			auto buffer = std::make_unique<uint8_t[]>(len);

			if (!buffer)
			{
				throw std::exception("unable to allocate for ipc.get");
			}

			// Attempt to get our key.
			auto result = db.get(
				key.c_str(),
				buffer.get(),
				len
			);

			// Check if our result was successful.
			if (!result) RETURN_NULL
			
			/////////////////////////////////////////////

			// Setup our deserializer delegate.
			DeserializerDelegate deserializer_delegate(args.GetIsolate());

			// Setup our value deserializer.
			v8::ValueDeserializer deserializer(
				args.GetIsolate(), 
				buffer.get(),
				len,
				&deserializer_delegate
			);

			// Attempt to read the value from the deserializer.
			auto value = deserializer.ReadValue(args.GetIsolate()->GetCurrentContext());

			// Check if our value isn't empty, throw an exception otherwise.
			if (value.IsEmpty()) throw std::exception("unable to deserialize value for ipc.get");

			// Return our value using an escapable handle.
			args.GetReturnValue().Set(
				escapable_handle_scope.Escape(
					value.ToLocalChecked()
				)
			);
		});

		////////////////////////////////////////

		// ipc Object
		global.set_const("ipc", ipc_module);

		// http Object
		global.set_const("http", http_module);

		////////////////////////////////////////

		return v8::Context::New(isolate, nullptr, global.obj_);
	}

	/**
	 * Initializes two global objects named http_response and http_request.
	 * They are the wrappers for IHttpRequest and IHttpResponse.
	 */
	void initialize_objects()
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

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
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for clear");

				HTTP_RESPONSE->Clear();
			});		

			// clearHeaders(): void
			module.set("clearHeaders", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for clearHeaders");

				HTTP_RESPONSE->ClearHeaders();
			});

			// closeConnection(): void
			module.set("closeConnection", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for closeConnection");
					
				HTTP_RESPONSE->CloseConnection();
			});

			// disableBuffering(): void
			module.set("disableBuffering", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for disableBuffering");

				HTTP_RESPONSE->DisableBuffering();
			});		
			
			// setNeedDisconnect(): void
			module.set("setNeedDisconnect", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for setNeedDisconnect");
					
				HTTP_RESPONSE->SetNeedDisconnect();
			});

			// getKernelCacheEnabled(): bool
			module.set("getKernelCacheEnabled", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for getKernelCacheEnabled");

				return bool(HTTP_RESPONSE->GetKernelCacheEnabled());
			});	

			// resetConnection(): void
			module.set("resetConnection", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for resetConnection");
					
				HTTP_RESPONSE->ResetConnection();
			});
				
			// getStatus(): Number
			module.set("getStatus", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for getStatus");

				// Our status code...
				USHORT status_code = 0;

				// Get our status code...
				HTTP_RESPONSE->GetStatus(&status_code);

				// Return our result. 
				return status_code; 
			});

			// redirect(url: String, resetStatusCode: bool, includeParameters: bool): void
			module.set("redirect", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for redirect");

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
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for setErrorDescription");

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
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for disableKernelCache");

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
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for deleteHeader");

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
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for getHeader");

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
				// REMARK:
				// This function can only be called in the SEND_RESPONSE callback
				// since that is the only time we can reliably read
				// the data that was written to the response.
				//
				// The SEND_RESPONSE callback is called for each chunk, 
				// therefore at any time you will only have at most one chunk to read.
				//
				// Because of this, the code only chooses the first item in the pEntityChunk array.
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for read");

				////////////////////////////////////////////////

				auto entity_chunk_count = HTTP_RESPONSE->GetRawHttpResponse()->EntityChunkCount;
				
				////////////////////////////////////////////////
				
				if (!entity_chunk_count) RETURN_NULL

				////////////////////////////////////////////////

				auto chunk = HTTP_RESPONSE->GetRawHttpResponse()->pEntityChunks[0];

				////////////////////////////////////////////////
				
				if (chunk.DataChunkType != HttpDataChunkFromMemory) RETURN_NULL
				if (!chunk.FromMemory.BufferLength) RETURN_NULL

				////////////////////////////////////////////////			

				// Whether to return our buffer as a Uint8Array or as a String.
				// Uint8Array output is useful to further be able
				// to decompress a gzip response.
				bool asArray = v8pp::from_v8<bool>(isolate, args[0], false);

				////////////////////////////////////////////////

				if (asArray)
				{
					auto array_buffer = v8::ArrayBuffer::New(
						isolate, 
						size_t(chunk.FromMemory.BufferLength)
					);

					////////////////////////////////////////////////
					
					std::memcpy(
						array_buffer->GetContents().Data(), 
						chunk.FromMemory.pBuffer, 
						chunk.FromMemory.BufferLength
					);

					////////////////////////////////////////////////

					auto uint8_array = v8::Uint8Array::New(
						array_buffer, 
						0, 
						size_t(chunk.FromMemory.BufferLength)
					);

					////////////////////////////////////////////////

					args.GetReturnValue().Set(
						uint8_array
					);
				}
				else
				{
					auto string = v8pp::to_v8(
						isolate, 
						(char*)chunk.FromMemory.pBuffer, 
						chunk.FromMemory.BufferLength
					);

					////////////////////////////////////////////////

					args.GetReturnValue().Set(
						string
					);
				}
			});
			
			// write(body: String || Uint8Array, mimetype: String, contentEncoding: String {optional}): void
			module.set("write", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for write");

				// Check arguments.
				if (args.Length() < 2 || !args[1]->IsString()) throw std::exception("invalid signature for write");

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

				// Get our mimetype.
				v8::String::Utf8Value mime_type(isolate, args[1]);

				// Check the length of the mime type.
				if (!*mime_type) throw std::exception("second argument is invalid for write");

				// Clear and set our header...
				HTTP_RESPONSE->SetHeader(HttpHeaderContentType, *mime_type, mime_type.length(), TRUE);

				////////////////////////////////////////////////

				// Check if content encoding parameter was provided.
				if (args.Length() >= 3) 
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
				if (!HTTP_RESPONSE) throw std::exception("invalid p_http_response for setHeader");

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
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for read");

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

			// deleteHeader(headerName: String): void
			module.set("deleteHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for deleteHeader");

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
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for setHeader");

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
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getMethod");

				RETURN_THIS(
					std::string(HTTP_REQUEST->GetHttpMethod())
				)
			});

			// getAbsPath(): String
			module.set("getAbsPath", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getMethod");

				RETURN_THIS(
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pAbsPath,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.AbsPathLength / sizeof(wchar_t)
					)
				)
			});
			 
			// getFullUrl(): String
			module.set("getFullUrl", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getFullUrl");

				RETURN_THIS(
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pFullUrl,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.FullUrlLength / sizeof(wchar_t)
					)
				)
			}); 

			// getQueryString(): String
			module.set("getQueryString", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getQueryString");

				RETURN_THIS( 
					std::wstring(
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.pQueryString,
						HTTP_REQUEST->GetRawHttpRequest()->CookedUrl.QueryStringLength / sizeof(wchar_t)
					)
				)
			});

			// getHost(): String
			module.set("getHost", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getHost");

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
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getLocalAddress");

				RETURN_THIS(
					sock_to_ip(
						HTTP_REQUEST->GetLocalAddress()
					)
				)
			}); 

			// getRemoteAddress(): String
			module.set("getRemoteAddress", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our pointer is valid...
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getRemoteAddress");
				
				RETURN_THIS(
					sock_to_ip(
						HTTP_REQUEST->GetRemoteAddress()
					)
				) 
			});

			// getHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				if (!HTTP_REQUEST) throw std::exception("invalid p_http_request for getHeader");
				
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
	int handle_callback(CALLBACK_TYPES type, IHttpContext * pHttpContext, void * pProvider)
	{
		if (!isolate) return RQ_NOTIFICATION_CONTINUE;

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
		
		if (!callback_function || callback_function->IsEmpty()) return RQ_NOTIFICATION_CONTINUE;

		////////////////////////////////////////////////

		asyncCompletionPending = false;
		
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

		// Update the pointers in our objects.
		http_response_object->SetAlignedPointerInInternalField(0, pHttpContext);
		http_request_object->SetAlignedPointerInInternalField(0, pHttpContext);

		////////////////////////////////////////////////
		
		// Setup local function...
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
			arguments[2] = v8pp::to_v8(isolate, ((ISendResponseProvider*)pProvider)->GetFlags());
		}

		////////////////////////////////////////////////

		// Attempt to get our registered our callback and call it.
		auto result = local_function->Call(
			isolate->GetCurrentContext(),
			v8::Null(isolate),
			argument_count,
			arguments
		);

		// Check if our function returned anything...
		if (result.IsEmpty()) return RQ_NOTIFICATION_CONTINUE;

		////////////////////////////////////////////////

		// Get our result value.
		auto result_value = result.ToLocalChecked();

		////////////////////////////////////////////////
		
		if (type == PRE_BEGIN_REQUEST && !result_value->IsNumber())
		{
			v8pp::throw_ex(isolate, "The PRE_BEGIN_REQUEST callback can only return a GLOBAL_NOTIFICATION_STATUS");

			////////////////////////////////////////////////

			return GL_NOTIFICATION_CONTINUE;
		}

		////////////////////////////////////////////////

		// Check if our result is a promise.
		if (result_value->IsPromise())
		{	
			// Get our promise object.
			auto promise = result_value.As<v8::Promise>();
			auto state = promise->State();

			// Check if our promise is already fulfilled or rejected.
			if (state == v8::Promise::kFulfilled || state == v8::Promise::kRejected)
			{
				// Cast our value to a request notification...
				return REQUEST_NOTIFICATION_STATUS(
					v8pp::from_v8<int>(isolate, promise->Result(), 0)
				);
			}

			//////////////////////////////////////////////////////

			// Our callback returned from the promise.
			auto callback = [](const v8::FunctionCallbackInfo<v8::Value>& args)
			{
				// Set our default notification status.
				int request_notification_status = v8pp::from_v8<int>(isolate, args[0], 0);

				// Cast our given Data,
				auto http_context = (IHttpContext*)args.Data().As<v8::External>()->Value();
					
				// Regardless of any result,
				// we need to indicate that the we've completed
				// our execution to IIS.
				http_context->IndicateCompletion(
					REQUEST_NOTIFICATION_STATUS(request_notification_status)
				); 
			};

			////////////////////////////////////////////////

			// Create our callback function.
			auto function = v8::Function::New(
				isolate->GetCurrentContext(),
				callback,
				v8::External::New(isolate, pHttpContext)
			).ToLocalChecked();
			
			// Attach our callback function to our promise to handle both scenarios.
			promise->Then(isolate->GetCurrentContext(), function, function);

			////////////////////////////////////////////////

			return RQ_NOTIFICATION_PENDING;
		}

		////////////////////////////////////////////////

		auto return_int_value = v8pp::from_v8<int>(isolate, result_value, 0);

		////////////////////////////////////////////////

		if (type != PRE_BEGIN_REQUEST && return_int_value == RQ_NOTIFICATION_PENDING)
		{
			v8pp::throw_ex(
				isolate, 
				"You cannot return \"RQ_NOTIFICATION_PENDING\", it is reserved for internal use only."
			);

			///////////////////////////////////////////
			
			return RQ_NOTIFICATION_CONTINUE;
		}

		///////////////////////////////////////////
		
		return return_int_value;
	}

	/**
	 * Converts a PSOCKADDR to a formatted string,
	 * works for both IPv4 and IPv6.
	 */
	std::string sock_to_ip(PSOCKADDR address)
	{
		if (!address) throw std::exception("invalid address for sock_to_ip");

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
		
		throw std::exception("invalid family for sock_to_ip");
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