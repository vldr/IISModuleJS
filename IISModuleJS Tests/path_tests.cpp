#include "CppUnitTest.h"
#include "helpers.h"
#include <httplib/httplib.h>
#include <rpc/client.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(PathTests)
{
public:
	TEST_METHOD(GetAbsPath)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getAbsPath(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/this/is/a/absolute/path?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "/this/is/a/absolute/path",
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "/",
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "/",
				true
			);
		}
	}

	TEST_METHOD(GetMethod)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getMethod(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "GET",
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Post("/", "", "text/html");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "POST",
				true
			);
		}
	}

	TEST_METHOD(GetFullUrl)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getFullUrl(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/this/is/a/absolute/path?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::string("http://")
				+ std::string(HOST)
				+ std::string(":80/this/is/a/absolute/path?this=is&a=query&string"),
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::string("http://") 
				+ std::string(HOST)
				+ std::string(":80/?this=is&a=query&string"),
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::string("http://")
				+ std::string(HOST)
				+ std::string(":80/"),
				true
			);
		}
	}

	TEST_METHOD(GetQueryString)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getQueryString(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/this/is/a/absolute/path?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "?this=is&a=query&string",
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "?this=is&a=query&string",
				true
			);
		}
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body.empty(),
				true
			);
		}
	}

	TEST_METHOD(GetHost)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getHost(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/this/is/a/absolute/path?this=is&a=query&string");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::string(HOST) + std::string(":80"),
				true
			);
		}
	}

	TEST_METHOD(GetLocalAddress)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getLocalAddress(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::string(HOST),
				true
			);
		}
		{
			httplib::Client http_client(IPV6_HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				std::string("[") + response->body + std::string("]") == IPV6_HOST,
				true
			);
		}
	}

	TEST_METHOD(GetRemoteAddress)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write('' + request.getRemoteAddress(), 'text/html');
			return FINISH;
		});
		)");

		//////////////////////////////////////////////
		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::string(HOST),
				true
			);
		}
		{
			httplib::Client http_client(IPV6_HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				std::string("[") + response->body + std::string("]") == IPV6_HOST,
				true
			);
		}
	}
};
