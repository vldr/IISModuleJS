#pragma once

#ifdef _DEBUG
#pragma comment(lib, "v8_monolith.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

#else
#pragma comment(lib, "v8_monolith.release.lib")
#endif

#define _WINSOCKAPI_
#include <windows.h>
#include <sal.h>
#include <ws2tcpip.h>
#include <httpserv.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <ctime>
#include <mutex>
#include <stdio.h>
#include <netfw.h>
#include <sstream>
#include <atlcomcli.h>

#include <cassert>
#include <libplatform/libplatform.h>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>
#include <thread>

#define ReturnNULL { args.GetReturnValue().Set(v8::Null(isolate));return; }
#define ReturnThis(value) args.GetReturnValue().Set(v8pp::to_v8(isolate, value)); return;

namespace v8_wrapper
{
	int __cdecl vs_printf(const char *format, ...);

	REQUEST_NOTIFICATION_STATUS begin_request(IHttpResponse * pHttpResponse, IHttpRequest * pHttpRequest);
	void register_begin_request(const v8::FunctionCallbackInfo<v8::Value>& args);

	void start();
	void initialize_objects();

	void read_file(const char* name);
	void load(const v8::FunctionCallbackInfo<v8::Value>& args);
	void print(const v8::FunctionCallbackInfo<v8::Value>& args);
	void report_exception(v8::TryCatch * try_catch);

	bool execute_string(char * str, bool print_result, bool report_exceptions);
	const char* c_string(v8::String::Utf8Value& value);

	v8::Local<v8::Context> create_shell_context();
}