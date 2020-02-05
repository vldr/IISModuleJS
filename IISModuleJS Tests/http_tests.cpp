#include "CppUnitTest.h"
#include "helpers.h"
#include <httplib/httplib.h>
#include <rpc/client.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(HTTPTests)
{
public:
	TEST_METHOD(Await)
	{
		EXECUTE_SCRIPT(R"(
		register(async (response, request) => {
			if (request.getAbsPath() == "/secret") 
			{
				response.setHeader("x-test", "value");
				response.write("hi", "text/html"); 
			}
			else
			{
				const secret = await http.fetch(")" + std::string(HOST) + R"(", "/secret")
					.then((response) =>
					 `${response.text()}, ${response.blob()[0]}${response.blob()[1]}, `
						+ `${response.status()}, ${response.headers()["x-test"]}`);

				response.write(secret, "text/html");
			}

			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/fetch");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(response->body.c_str(), "hi, 104105, 200, value");
		}
	}

	TEST_METHOD(SendResponse_Await)
	{
		EXECUTE_SCRIPT(R"(
		register(() => { return FINISH });
		register(1, async (response, request) => {
			if (request.getAbsPath() == "/secret") 
			{
				response.write("secret", "text/html");
			}
			else
			{
				const secret = await http.fetch(")" + std::string(HOST) + R"(", "/secret")
					.then((response) => response.text());

				response.write(secret, "text/html");
			}

			return FINISH;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/fetch");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "secret",
				true
			);
		}
	}
};
