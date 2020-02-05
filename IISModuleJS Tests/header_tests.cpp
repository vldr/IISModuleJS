#include "CppUnitTest.h"
#include "helpers.h"
#include <httplib/httplib.h>
#include <rpc/client.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(HeaderTests)
{
public:
	TEST_METHOD(SetHeader)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader('x-test-header', 'header value', false);

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("x-test-header") != response->headers.end() &&
			response->headers.find("x-test-header")->second == "header value",
			true
		);
	}

	TEST_METHOD(SetStatus)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setStatus(505, "Test Message");

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->status == 505,
			true
		);
	}

	TEST_METHOD(SetHeaderReplace)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader('Server', 'new server', true);

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("Server") != response->headers.end() &&
			response->headers.find("Server")->second == "new server",
			true
		);
	}

	TEST_METHOD(SetHeaderAppend)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader('Server', 'new server', false);

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.count("Server") == 2,
			true
		);
	}

	TEST_METHOD(SetHeaderEmpty)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader('x-test-header', '', false);

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.count("x-test-header") == 0,
			true
		);
	}

	TEST_METHOD(DeleteHeader)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.deleteHeader('Server');

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.count("Server") == 0,
			true
		);
	}

	TEST_METHOD(SetHeaderRequest)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader(
				"x-before-set", 
				request.getHeader('test-header')
			);

			request.setHeader('test-header', 'new value');

			response.setHeader(
				"x-after-set", 
				request.getHeader('test-header')
			);

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Headers headers;
		headers.emplace("test-header", "header value");

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/", headers);

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("x-before-set") != response->headers.end() &&
			response->headers.find("x-before-set")->second == "header value",
			true
		);

		Assert::AreEqual(
			response->headers.find("x-after-set") != response->headers.end() &&
			response->headers.find("x-after-set")->second == "new value",
			true
		);
	}
	
	TEST_METHOD(DeleteHeaderRequest)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.setHeader(
				"x-before-delete", 
				request.getHeader('test-header')
			);

			request.deleteHeader('test-header');

			response.setHeader(
				"x-after-delete", 
				`${request.getHeader('test-header')}`
			);

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Headers headers;
		headers.emplace("test-header", "header value");

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/", headers);

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("x-before-delete") != response->headers.end() &&
			response->headers.find("x-before-delete")->second == "header value",
			true
		);

		Assert::AreEqual(
			response->headers.find("x-after-delete") != response->headers.end() &&
			response->headers.find("x-after-delete")->second == "null",
			true
		);
	}

	TEST_METHOD(GetHeader)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader(
		        'x-test-header', 
		        request.getHeader('test-header')
		    );

		    return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Headers headers;
		headers.emplace("test-header", "header value");

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/", headers);

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("x-test-header") != response->headers.end() &&
			response->headers.find("x-test-header")->second == "header value",
			true
		);
	}

	TEST_METHOD(ClearHeaders)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader(
		        'x-test-header', 
		        'test-value'
		    );
			response.clearHeaders();
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("x-test-header") == response->headers.end(),
			true
		);
	}

	TEST_METHOD(GetHeaderResponse)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader(
		        'x-test-header', 
		        'test-value'
		    );
			response.write('' + response.getHeader('x-test-header'), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->body == "test-value",
			true
		);
	}

	TEST_METHOD(Redirect)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.redirect("header value", true, true);
			return CONTINUE;
		});
		)");

		//////////////////////////////////////////////

		httplib::Client http_client(HOST);
		auto response = http_client.Get("/");

		if (!response) Assert::Fail(L"failed to get http response.");

		Assert::AreEqual(
			response->headers.find("location") != response->headers.end() &&
			response->headers.find("location")->second == "header value",
			true
		);
	}
};
