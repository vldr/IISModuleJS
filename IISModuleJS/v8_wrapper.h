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

#ifdef _DEBUG
#include <rpc/server.h>
#pragma comment(lib, "v8_monolith.64.lib")
#pragma comment(lib, "rpc.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
#else
#pragma comment(lib, "v8_monolith.release.64.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#endif

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")

#include <cassert>
#include <libplatform/libplatform.h>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>
#include <thread>
#include <experimental/filesystem>

#define RETURN_NULL { args.GetReturnValue().Set(v8::Null(isolate));return; }
#define RETURN_THIS(value) args.GetReturnValue().Set(v8pp::to_v8(isolate, value)); return;

#define HTTP_CONTEXT ((IHttpContext*)args.This()->GetAlignedPointerFromInternalField(0))
#define HTTP_REQUEST ((IHttpContext*)args.This()->GetAlignedPointerFromInternalField(0))->GetRequest()
#define HTTP_RESPONSE ((IHttpContext*)args.This()->GetAlignedPointerFromInternalField(0))->GetResponse()
#define FETCH_RESPONSE ((httplib::Response*)args.This()->GetAlignedPointerFromInternalField(0))
#define HTTP_RESPONSE_CHUNKS ((v8_wrapper::ResponseChunks*)args.This()->GetAlignedPointerFromInternalField(1))

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
		
		httplib::Response * m_response;
		v8::Persistent<v8::Object> response_object;
	};
	
	/**
	 * A struct representing the Fetch Init Object.
	 */
	struct FetchObject
	{
		
	};

	/**
	 * A class organizing all the chunks in the SEND_RESPONSE callback.
	 */
	class ResponseChunks
	{
	public:
		static constexpr unsigned int MAX_CHUNKS = 1024;

		// Represents the number of chunks in the response.
		unsigned int m_number_of_chunks = 0;

		// An array of pointers pointing to all the chunks.
		void* m_chunks[MAX_CHUNKS];

		// An array of uints containing the sizes of each chunk.
		unsigned int m_chunk_sizes[MAX_CHUNKS];

		// The total number of bytes of chunks there are.
		size_t m_total_size = 0;
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
	
	bool is_registered(CALLBACK_TYPES type);
	int handle_callback(CALLBACK_TYPES type, IHttpContext * pHttpContext, void * pCustomObject = nullptr);

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
	int vs_printf(const char *format, ...);

	v8::Local<v8::Context> create_shell_context();
}