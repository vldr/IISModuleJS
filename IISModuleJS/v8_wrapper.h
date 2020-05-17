#pragma once
#pragma warning( disable : 4267 )
#pragma warning( disable : 4244 )

#define _WINSOCKAPI_
#define NOMINMAX
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <windows.h>
#include <sal.h>
#include <ws2tcpip.h>
#include <httpserv.h>
#include <vector>
#include <string> 
#include <Shlobj.h>
#include <httplib/httplib.h>
#include <Shlwapi.h>
 
#pragma comment(lib, "sqlite3.lib")

#ifdef _DEBUG
#include <rpc/server.h>
#pragma comment(lib, "v8_monolith.64.lib")
#pragma comment(lib, "rpc.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "libcppdb.lib")
#else
#pragma comment(lib, "v8_monolith.release.64.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "libcppdb.release.lib")
#endif

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "Shlwapi.lib")

#include <cassert>
#include <libplatform/libplatform.h>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>
#include <cppdb/frontend.h>
#include <thread>
#include <iostream>
#include <sstream>

#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>

#define DISABLE_INTERNAL_POINTER_RESET

#define RETURN_NULL { args.GetReturnValue().Set(v8::Null(isolate));return; }
#define RETURN_THIS(value) args.GetReturnValue().Set(v8pp::to_v8(isolate, value)); return;

#define HTTP_CONTEXT ((IHttpContext*)args.This()->GetAlignedPointerFromInternalField(0))
#define HTTP_REQUEST HTTP_CONTEXT->GetRequest()
#define HTTP_RESPONSE HTTP_CONTEXT->GetResponse()

#ifndef DISABLE_INTERNAL_POINTER_RESET

#define RESET_INTERNAL_POINTERS http_response_object->SetAlignedPointerInInternalField(0, nullptr); \
http_request_object->SetAlignedPointerInInternalField(0, nullptr); 

#else

#define RESET_INTERNAL_POINTERS

#endif

#define FETCH_RESPONSE ((httplib::Response*)args.This()->GetAlignedPointerFromInternalField(0))
#define DB_CONTEXT ((DbContext*)args.This()->GetAlignedPointerFromInternalField(0))

#define pmax(a,b) (((a) > (b)) ? (a) : (b))
#define pmin(a,b) (((a) < (b)) ? (a) : (b))

namespace v8_wrapper
{
	/**
	 * An enum representing different types
	 * of callbacks.
	 */
	enum CALLBACK_TYPES
	{
		BEGIN_REQUEST,
		SEND_RESPONSE,
		PRE_BEGIN_REQUEST
	};
	 
	/**
	 * An enum representing different types
	 * of fetch return types.
	 */
	enum DB_DATA_TYPES
	{
		STRING,
		INTEGER,
		DOUBLE,
		BOOL,
		BINARY
	};


	/**
	 * A class which allows the ability to allocate an
	 * external char array to be used with a v8::String.
	 */
	class ExternalString : public v8::String::ExternalOneByteStringResource
	{
	public:
		ExternalString(size_t length) 
			: data_(new char[length]), length_(length), was_allocated_(true) {}

		ExternalString(const char* data, size_t length) 
			: data_((char*)data), length_(length), was_allocated_(false) {}

		~ExternalString() override
		{
			if (was_allocated_)
				delete[] data_;
		}

		const char* data() const override
		{
			return (const char*)data_;
		}

		size_t length() const override
		{
			return length_;
		}
	private:
		char* data_;
		size_t length_;
		bool was_allocated_;
	};

	/**
	* A class that handles the IHttpContext object.
	*/
	class HttpContextHandler 
	{
	public:
		HttpContextHandler() : m_http_context(nullptr) {}

		HttpContextHandler(IHttpContext * http_context)
			: m_http_context(http_context)  {}

		void set(IHttpContext * http_context)
		{
			m_http_context = http_context;
		}

		void reset()
		{
			m_http_context = nullptr;

			delete this;
		}

		IHttpContext * get()
		{
			return m_http_context;
		}

	private:
		IHttpContext * m_http_context;
	};


	/**
	 * A class representing the http.fetch request object.
	 */
	class FetchRequest
	{
	public:
		FetchRequest(std::string hostname, std::string path)
		: hostname(std::move(hostname)), path(std::move(path))
		{};

		bool is_ssl = false;
		std::string hostname;
		std::string path;
		std::string method = "GET";
		std::string body;
		httplib::Headers headers;
	};

	/**
	* A wrapper on top of cppdb::session.
	*/
	class DbSession : public cppdb::session
	{
	public:
		~DbSession()
		{
			this->close();
		}
	};

	/**
	* A struct that will manage the statement and session objects.
	*/ 
	struct DbContext {
		DbSession session = DbSession();
		cppdb::statement statement = cppdb::statement();
		cppdb::result result = cppdb::result::result();
	};

	/**
	 * A class that manages the everything related to the db object.
	 */
	class DbHandler
	{
	public:
		DbHandler(
			v8::Isolate* isolate,
			v8::Local<v8::Object> object,
			DbContext * context
		) : m_context(context), db_object(isolate, object)
		{
			object->SetAlignedPointerInInternalField(0, context);
		}

		~DbHandler()
		{
			delete m_context;
		}

		int64_t capacity()
		{
			return sizeof DbContext;
		}

		DbContext * m_context;
		v8::Persistent<v8::Object> db_object;
	};

	/**
	 * A class that manages the httplib::Response object and
	 * a persistent handle to the fetch response object.
	 */
	class FetchResponse
	{
	public:
		FetchResponse(
			v8::Isolate* isolate,
			v8::Local<v8::Object> object,
			httplib::Response * response
		) : m_response(response), response_object(isolate, object)
		{
			object->SetAlignedPointerInInternalField(0, m_response);
		}
		
		~FetchResponse()
		{
			delete m_response;
		}

		int64_t capacity()
		{
			int64_t sum = 0;
			sum += m_response->body.capacity();
			sum += m_response->version.capacity();
			sum += sizeof m_response->status;
			sum += (sizeof(std::string) + sizeof(std::pair<std::string, std::string>)) * m_response->headers.size();

			return sum;
		}
		
		httplib::Response * m_response;
		v8::Persistent<v8::Object> response_object;
	};
	
	/**
	 * The delegate that handles deserialization.
	 */
	class DeserializerDelegate : public v8::ValueDeserializer::Delegate
	{
	public:
		explicit DeserializerDelegate(v8::Isolate * isolate) : _isolate(isolate) {}
	private:
		v8::Isolate * _isolate;
	};

	/**
	 * The delegate that handles serialization.
	 */
	class SerializerDelegate : public v8::ValueSerializer::Delegate
	{
	public:
		explicit SerializerDelegate(v8::Isolate * isolate) : _isolate(isolate) {}
		void ThrowDataCloneError(v8::Local<v8::String> message)
		{
			_isolate->ThrowException(v8::Exception::Error(message));
		}
	private:
		v8::Isolate * _isolate;
	};

	/**
	 * Utf8ValueScoped keeps track of a v8::String::Utf8Value
	 * and provides operators for ease of readability.
	 */
	class Utf8ValueScoped
	{
	public:
		Utf8ValueScoped(v8::Isolate * isolate, v8::Local<v8::Value> obj)
		{
			if (obj->IsString())
				m_utf8_value = new v8::String::Utf8Value(isolate, obj);
		}

		operator void*() const
		{
			return **m_utf8_value;
		}

		operator bool() const
		{
			return m_utf8_value != nullptr;
		}

		operator unsigned long() const
		{
			return m_utf8_value->length();
		}

		~Utf8ValueScoped()
		{
			delete m_utf8_value;
		}
	private:
		v8::String::Utf8Value * m_utf8_value;
	};

	/**
	 * ArrayBufferScoped keeps track of v8::ArrayBuffer's contents
	 * and provides operators for ease of readability.
	 */
	class ArrayBufferScoped
	{
	public:
		ArrayBufferScoped(v8::Local<v8::Value> obj)
		{
			if (obj->IsUint8Array())
			{
				m_is_array = true;
				m_array_buffer = obj.As<v8::Uint8Array>()->Buffer()->GetContents();
			}
		}

		operator bool() const
		{
			return m_is_array;
		}

		operator unsigned long() const
		{
			return m_array_buffer.ByteLength();
		}

		operator void*() const
		{
			return m_array_buffer.Data();
		}

	private:
		bool m_is_array = false;
		v8::ArrayBuffer::Contents m_array_buffer;
	};

	const v8::Eternal<v8::Name>* find_or_create_eternal_name_cache(
		const void* lookup_key,
		const char* const names[],
		size_t count);
	
	int handle_callback(CALLBACK_TYPES type, IHttpContext * pHttpContext, void * pObject);

	void start(std::wstring app_pool_name);
	void reset_engine();
	void load_and_watch();
	void initialize_objects();

	void directory_change_callback();
	std::experimental::filesystem::path& get_relative_file_path(std::wstring &raw_input);

	std::experimental::filesystem::path get_path(std::wstring script);
	void execute_file(std::experimental::filesystem::path & script_path);
	void report_exception(v8::TryCatch * try_catch);

	std::string sock_to_ip(PSOCKADDR address);
	bool execute_string(const char * script_name, char * str);
	const char* c_string(v8::String::Utf8Value& value);
	int vs_printf(const char *format, ...);

	v8::Local<v8::Context> create_shell_context();
}