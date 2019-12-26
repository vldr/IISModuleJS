#include "CppUnitTest.h"
#include "helpers.h"

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
