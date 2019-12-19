#pragma once

#ifdef _DEBUG
#pragma comment(lib, "v8_monolith.64.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
#else
#pragma comment(lib, "v8_monolith.release.64.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#endif


#define _WINSOCKAPI_
#include <windows.h>
#include <sal.h>
#include <ws2tcpip.h>
#include <httpserv.h>
#include <vector>
#include <string>
#include <Shlobj.h>

#include <cassert>
#include <libplatform/libplatform.h>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>
#include <thread>
#include <experimental/filesystem>

#define RETURN_NULL { args.GetReturnValue().Set(v8::Null(isolate));return; }
#define RETURN_THIS(value) args.GetReturnValue().Set(v8pp::to_v8(isolate, value)); return;

namespace v8_wrapper
{
	int __cdecl vs_printf(const char *format, ...);

	REQUEST_NOTIFICATION_STATUS begin_request(IHttpResponse * pHttpResponse, IHttpRequest * pHttpRequest);

	void start(std::wstring app_pool_name);
	void reset_engine();
	void load_and_watch();
	void initialize_objects();

	std::experimental::filesystem::path get_path(std::wstring script);
	void execute_file(const wchar_t * name);
	void report_exception(v8::TryCatch * try_catch);

	std::string sock_to_ip(PSOCKADDR address);
	bool execute_string(char * str, bool print_result, bool report_exceptions);
	const char* c_string(v8::String::Utf8Value& value);

	v8::Local<v8::Context> create_shell_context();
}