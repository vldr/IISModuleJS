#include "CppUnitTest.h"
#include "helpers.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(HeaderTests)
{
public:
	TEST_METHOD(SetHeader)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader('x-test-header', 'header value', false);

		    return RQ_NOTIFICATION_FINISH_REQUEST;
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

	TEST_METHOD(SetHeaderReplace)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader('Server', 'new server', true);

		    return RQ_NOTIFICATION_FINISH_REQUEST;
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

		    return RQ_NOTIFICATION_FINISH_REQUEST;
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

		    return RQ_NOTIFICATION_FINISH_REQUEST;
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

		    return RQ_NOTIFICATION_FINISH_REQUEST;
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

	TEST_METHOD(GetHeader)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
		    response.setHeader(
		        'x-test-header', 
		        request.getHeader('test-header')
		    );

		    return RQ_NOTIFICATION_FINISH_REQUEST;
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

	TEST_METHOD(Redirect)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.redirect("header value", true, true);
			return RQ_NOTIFICATION_CONTINUE;
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
