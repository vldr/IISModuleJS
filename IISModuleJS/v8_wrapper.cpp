#include "v8_wrapper.h"

namespace v8_wrapper
{
	v8::Global<v8::Object> global_http_response_object;
	v8::Global<v8::Object> global_http_request_object;

	v8::Global<v8::Function> function_begin_request;
	v8::Persistent<v8::Context> context;

	v8::Isolate * isolate = nullptr;

	IHttpResponse * p_http_response = nullptr;
	IHttpRequest * p_http_request = nullptr;

	void start()
	{
		std::thread engine_thread([] {
			// Initialize...
			std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();

			v8::V8::InitializePlatform(platform.get());
			v8::V8::InitializeICU();
			v8::V8::Initialize();

			// Setup create params...
			v8::Isolate::CreateParams create_params;

			create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

			// Setup our isolate...
			isolate = v8::Isolate::New(create_params);

			{
				// Setup lockers...
				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);

				// Setup our context...
				context.Reset(isolate, create_shell_context());
			}

			// Setup script file name...
			const auto file_name = "test.js";

			// Initialize our objects...
			initialize_objects();

			// Read our file...
			read_file(file_name);

			//////////////////////////////////////////
			
			// Our current directory...
			char current_directory[MAX_PATH + 1];

			// Get our current directory...
			GetCurrentDirectoryA(MAX_PATH, current_directory);

			// Set our null terminator...
			current_directory[MAX_PATH] = '\0';

			// Setup our last write change handle...
			HANDLE change_handle = FindFirstChangeNotificationA(_T(current_directory), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);

			// Loop forever...
			for (;;)
			{
				// Wait infinitely for our change handle...
				DWORD wait_handle = WaitForSingleObject(change_handle, INFINITE);

				if (wait_handle == WAIT_OBJECT_0)
				{
					// Inform user...
					vs_printf("Reloading \"%s\" script...\n", file_name);

					// Read our file again...
					read_file(file_name);

					// Find the next change notification...
					FindNextChangeNotification(change_handle);
				}
				else
				{
					break;
				}
			}
		});
		engine_thread.detach();

		/*
			// Dispose our isolate...
			isolate->Dispose();

			// Dispose everything...
			v8::V8::Dispose();
			v8::V8::ShutdownPlatform();

			// Delete...
			delete create_params.array_buffer_allocator;
		*/
	}

	v8::Local<v8::Context> create_shell_context()
	{
		// Setup isolate locker...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);

		// Create a template for the global object.
		v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

		// Bind the global 'print' function to the C++ Print callback.
		global->Set(v8::String::NewFromUtf8(isolate, "print").ToLocalChecked(), v8::FunctionTemplate::New(isolate, print));

		// Bind the global 'load' function to the C++ Load callback.
		global->Set(v8::String::NewFromUtf8(isolate, "load").ToLocalChecked(), v8::FunctionTemplate::New(isolate, load));

		////////////////////////////////

		// Bind the 'registerDraw' function
		global->Set(v8::String::NewFromUtf8(isolate, "registerBeginRequest").ToLocalChecked(),
			v8::FunctionTemplate::New(isolate, register_begin_request));

		return v8::Context::New(isolate, NULL, global);
	}

	void initialize_objects()
	{
		// Setup.
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
				// Check if our http response is set.
				if (p_http_response) 
				{
					p_http_response->Clear();
				}
			});

			// clearHeaders(): void
			module.set("clearHeaders", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (p_http_response) 
				{
					p_http_response->ClearHeaders();
				}
			});

			// closeConnection(): void
			module.set("closeConnection", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (p_http_response) 
				{
					p_http_response->CloseConnection();
				}
			});

			// setNeedDisconnect(): void
			module.set("setNeedDisconnect", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (p_http_response) 
				{
					p_http_response->SetNeedDisconnect();
				}
			});

			// resetConnection(): void
			module.set("resetConnection", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (p_http_response) 
				{
					p_http_response->ResetConnection();
				}
			});

			// disableBuffering(): void
			module.set("disableBuffering", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (p_http_response) 
				{
					p_http_response->DisableBuffering();
				}
			});

			// disableKernelCache(reason: Number {optional}): void
			module.set("disableKernelCache", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (p_http_response) 
				{
					// Our reason for disabling.
					ULONG reason = 9;

					// Optional arguments.
					if (args.Length() == 1 && args[0]->IsNumber())
						reason = v8pp::from_v8<ULONG>(args.GetIsolate(), args[0]);

					// Disable kernel caching...
					p_http_response->DisableKernelCache(reason);
				}
			});


			// deleteHeader(headerName: String): bool
			module.set("deleteHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check arguments.
				if ( args.Length() != 1 || !args[0]->IsString() ) ReturnNULL

				// Check if our http response is set.
				if (!p_http_response) 
				{
					ReturnThis(false)
				}

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

				// Attempt to get our header. 
				auto hr = p_http_response->DeleteHeader(header_name.c_str());

				// Check if our header value is valid...
				ReturnThis(SUCCEEDED(hr))
			});

			// getHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check arguments.
				if ( args.Length() != 1 || !args[0]->IsString() ) ReturnNULL

				// Check if our http response is set.
				if ( !p_http_response ) ReturnNULL

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				
				// Header value count.
				USHORT header_value_count = 0;

				// Attempt to get our header. 
				auto header_value = p_http_response->GetHeader(header_name.c_str(), &header_value_count);

				// Check if our header value is valid...
				if (header_value)
				{
					ReturnThis(std::string(header_value, header_value_count))
				}

				// Return null otherwise.
				ReturnNULL
			});

			// setHeader(headerName: String, headerValue: String, shouldReplace: bool): bool
			module.set("setHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check arguments.
				if ( args.Length() != 3 
					|| !args[0]->IsString() 
					|| !args[1]->IsString() 
					|| !args[2]->IsBoolean() ) 
					ReturnNULL

				// Check if our http response is set.
				if ( !p_http_response ) ReturnNULL

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				auto header_value = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);
				auto should_replace = v8pp::from_v8<bool>(args.GetIsolate(), args[2]);

				// Attempt to get our header. 
				auto hr = p_http_response->SetHeader( header_name.c_str(), 
					header_value.c_str(), 
					header_value.length(), 
					should_replace );  

				ReturnThis(SUCCEEDED(hr))
			});

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

			// Gets the method
			// getMethod(): String
			module.set("getMethod", []() {
				// Check if our pointer is valid...
				if (!p_http_request) return std::string();

				// Convert our ip address to an std::string...
				return std::string(p_http_request->GetHttpMethod());
			});

			// getAbsPath(): String
			module.set("getAbsPath", []() {
				// Check if our pointer is valid...
				if (!p_http_request) return std::wstring();

				// Convert our ip address to an std::string...
				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pAbsPath, 
					p_http_request->GetRawHttpRequest()->CookedUrl.AbsPathLength);
			});

			// getFullUrl(): String
			module.set("getFullUrl", []() {
				// Check if our pointer is valid...
				if (!p_http_request) return std::wstring();

				// Convert our ip address to an std::string...
				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pFullUrl, 
					p_http_request->GetRawHttpRequest()->CookedUrl.FullUrlLength);
			});

			// getQueryString(): String
			module.set("getQueryString", []() {
				// Check if our pointer is valid...
				if (!p_http_request) return std::wstring();

				// Convert our ip address to an std::string...
				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pQueryString, 
					p_http_request->GetRawHttpRequest()->CookedUrl.QueryStringLength);
			});

			// getHost(): String
			module.set("getHost", []() {
				// Check if our pointer is valid...
				if (!p_http_request) return std::wstring();

				// Convert our ip address to an std::string...
				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pHost, 
					p_http_request->GetRawHttpRequest()->CookedUrl.HostLength);
			});

			// getRemoteAddress(): String
			module.set("getRemoteAddress", []() {
				// Check if our pointer is valid...
				if (!p_http_request) return std::string();
				
				// Cast our socket...
				sockaddr_in * socket = (sockaddr_in *)p_http_request->GetRemoteAddress();

				// Setup our ip address variable...
				char ip_address[INET_ADDRSTRLEN];

				// Get our ip address...
				InetNtop(socket->sin_family, &socket->sin_addr, ip_address, INET_ADDRSTRLEN);

				// Convert our ip address to an std::string...
				return std::string(ip_address);
			});

			// setHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check arguments.
				if (args.Length() != 1 || !args[0]->IsString()) ReturnNULL

				// Check if our http response is set.
				if (!p_http_request) ReturnNULL

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

				// Header value count.
				USHORT header_value_count = 0;

				// Attempt to get our header. 
				auto header_value = p_http_request->GetHeader(header_name.c_str(), &header_value_count);

				// Check if our header value is valid...
				if (header_value)
				{
					ReturnThis(std::string(header_value, header_value_count))
				}

				// Return null otherwise.
				ReturnNULL
			});

			// Reset our pointer...
			global_http_request_object.Reset(isolate, module.new_instance());
		}
	}

	void begin_request(IHttpResponse * pHttpResponse, IHttpRequest * pHttpRequest)
	{
		// Check if our pointers are null...
		if (!pHttpResponse || !pHttpRequest) return;

		// Check that isolate and the callback function is set...
		if (isolate && !function_begin_request.IsEmpty())
		{
			// Setup our lockers, isolate scope, and handle scope...
			v8::Locker locker(isolate);
			v8::Isolate::Scope isolate_scope(isolate);
			v8::HandleScope handle_scope(isolate);
			v8::Context::Scope context_scope(context.Get(isolate));
			
			////////////////////////////////////////////////

			// Get our object...
			auto http_response_object = global_http_response_object.Get(isolate);
			auto http_request_object = global_http_request_object.Get(isolate);

			// Setup our arguments...
			v8::Local<v8::Value> argv[2] = { http_response_object, http_request_object };

			// Setup local function...
			auto local_function = function_begin_request.Get(isolate);

			// Update our global pointers...
			p_http_request = pHttpRequest;
			p_http_response = pHttpResponse;

			// Attempt to get our registered create move callback and call it...
			if (local_function->IsCallable())
				local_function->Call(isolate->GetCurrentContext(), v8::Null(isolate), 2, argv);

			// Reset our pointers...
			p_http_request = nullptr;
			p_http_response = nullptr;
		}
	}

	bool execute_string(char * str, bool print_result, bool report_exceptions)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		// Enter the execution environment before evaluating any code.
		v8::Local<v8::String> name(v8::String::NewFromUtf8(isolate, "(shell)", v8::NewStringType::kNormal).ToLocalChecked());

		// Setup other related...
		v8::TryCatch try_catch(isolate);
		v8::ScriptOrigin origin(name);
		v8::Local<v8::Context> context(isolate->GetCurrentContext());
		v8::Local<v8::Script> script;

		// Setup our source...
		auto source = v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal).ToLocalChecked();

		// Empty out our function...
		function_begin_request.Empty();

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

			return false;
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

	void print(const v8::FunctionCallbackInfo<v8::Value>& args)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		bool first = true;
		for (int i = 0; i < args.Length(); i++)
		{
			v8::HandleScope handle_scope(isolate);
			if (first) {
				first = false;
			}
			else {
				vs_printf( " ");
			}
			v8::String::Utf8Value str(isolate, args[i]);
			const char* cstr = c_string(str);
			vs_printf( "%s", cstr);
		}
		vs_printf( "\n");
		fflush(stdout);
	}

	void register_begin_request(const v8::FunctionCallbackInfo<v8::Value>& args)
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		// Check if args given...
		if (args.Length() < 1) return;

		// Get our arguments...
		v8::Local<v8::Value> arg = args[0];

		// Check if arg is a function...
		if (!arg->IsFunction())
		{
			return;
		}

		// Reset our global function...
		function_begin_request.Reset(isolate, v8::Local<v8::Function>::Cast(arg));
	}

	void read_file(const char* name)
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		FILE* file = fopen(name, "rb");
		if (file == NULL) return;

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

		vs_printf( "Loaded %s successfully...\n", name);
		fflush(stdout);
	}

	void load(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::HandleScope handle_scope(isolate);

		for (int i = 0; i < args.Length(); i++)
		{
			v8::HandleScope handle_scope(isolate);
			v8::String::Utf8Value file(isolate, args[i]);
			if (*file == NULL) {
				isolate->ThrowException(
					v8::String::NewFromUtf8(isolate, "Error loading file",
						v8::NewStringType::kNormal).ToLocalChecked());
				return;
			}

			read_file(*file);
		}
	}

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
			vs_printf( "%s\n", exception_string);
		}
		else {
			v8::Local<v8::Context> context(isolate->GetCurrentContext());
			int linenum = message->GetLineNumber(context).FromJust();

			vs_printf( "%i: %s\n", linenum, exception_string);

			v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
			const char* sourceline_string = c_string(sourceline);
			vs_printf( "%s\n", sourceline_string);

			int start = message->GetStartColumn(context).FromJust();
			for (int i = 0; i < start; i++) {
				vs_printf( " ");
			}
			int end = message->GetEndColumn(context).FromJust();
			for (int i = start; i < end; i++) {
				vs_printf( "^");
			}
			vs_printf( "\n");
			v8::Local<v8::Value> stack_trace_string;
			if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
				stack_trace_string->IsString() &&
				v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0) {
				v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
				const char* stack_trace_string = c_string(stack_trace);
				vs_printf( "%s\n", stack_trace_string);
			}
		}
	}

	const char* c_string(v8::String::Utf8Value& value)
	{
		return *value ? *value : "<string conversion failed>";
	}

	int __cdecl vs_printf(const char *format, ...)
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