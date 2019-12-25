#include "CppUnitTest.h"
#include "helpers.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(HTTPTests)
{
public:
	TEST_METHOD(NoAwait)
	{
		EXECUTE_SCRIPT(R"(
		let secret;
		register((response, request) => {
			if (request.getAbsPath() == "/secret") 
			{
				response.write("secret", "text/html");
			}
			else if (request.getAbsPath() == "/fetch") 
			{
				http.fetch(")" + std::string(HOST) + R"(", "/secret", false)
					.then((response) => secret = response.body);

				response.write("fetch", "text/html");
			}
			else 
				response.write(secret, "text/html");

			return RQ_NOTIFICATION_FINISH_REQUEST;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/fetch");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "fetch",
				true
			);
		}

		// Sleep for one second since we are not using await 
		// so we cannot guarantee that our secret has been updated in time.
		std::this_thread::sleep_for(std::chrono::seconds(1));

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == "secret",
				true
			);
		}
	}

	TEST_METHOD(Await)
	{
		EXECUTE_SCRIPT(R"(
		register(async (response, request) => {
			if (request.getAbsPath() == "/secret") 
			{
				response.write("secret", "text/html");
			}
			else
			{
				const secret = await http.fetch(")" + std::string(HOST) + R"(", "/secret", false)
					.then((response) => response.body);

				response.write(secret, "text/html");
			}

			return RQ_NOTIFICATION_FINISH_REQUEST;
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
