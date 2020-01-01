#include "CppUnitTest.h"
#include "helpers.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
 
TEST_CLASS(IPCTests)
{
public:
	TEST_METHOD(SetAndGet)
	{
		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			ipc.set("test", {
				number: 3.14,
				text: "sample text",
				array: [ 3.14, "sample text" ]
			});
 
			response.write(
				JSON.stringify( 
					ipc.get("test")
				),
				"application/json"
			);

			return RQ_NOTIFICATION_FINISH_REQUEST;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == R"({"number":3.14,"text":"sample text","array":[3.14,"sample text"]})",
				true
			);
		}

		//////////////////////////////////////////////
		// Second pass to test persistence
		//////////////////////////////////////////////

		EXECUTE_SCRIPT(R"(
		register((response, request) => {
			response.write(
				JSON.stringify( 
					ipc.get("test")
				),
				"application/json"
			);

			return RQ_NOTIFICATION_FINISH_REQUEST;
		});
		)");

		//////////////////////////////////////////////

		{
			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == R"({"number":3.14,"text":"sample text","array":[3.14,"sample text"]})",
				true
			);
		}
	}
};
