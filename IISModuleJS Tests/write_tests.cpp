#include "CppUnitTest.h"
#include "helpers.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(WriteTests)
{
public:
	TEST_METHOD(WriteString)
	{
		auto random_string = [](size_t length) -> std::string
		{
			srand(time(nullptr));
			auto randchar = []() -> char
			{
				const char charset[] =
					"0123456789"
					"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					"abcdefghijklmnopqrstuvwxyz";
				const size_t max_index = (sizeof(charset) - 1);
				return charset[rand() % max_index];
			};
			std::string str(length, 0);
			std::generate_n(str.begin(), length, randchar);
			return str;
		};

		size_t test_sizes[] = { 0, 32768, 65536, 131072 };

		for (int i = 0; i < std::size(test_sizes); i++)
		{
			auto random_string_value = random_string(test_sizes[i]);

			std::string script = R"(
			register((response, request) => {
			    response.write(')" + random_string_value +  R"(', 'text/html');

			    return RQ_NOTIFICATION_FINISH_REQUEST;
			});
			)";

			EXECUTE_SCRIPT(std::move(script));

			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == random_string_value,
				true
			);
		}	
	}

	TEST_METHOD(WriteArray)
	{
		auto random_string_array = [](size_t length) -> std::tuple<std::string, std::string>
		{
			srand(time(nullptr));
			std::stringstream clean;
			std::stringstream ss;

			const char charset[] =
				"0123456789"
				"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				"abcdefghijklmnopqrstuvwxyz";

			for (size_t i = 0; i < length; i++)
			{
				const size_t max_index = (sizeof(charset) - 1);
				auto c = charset[rand() % max_index];
				ss << "0x";
				ss << std::setw(2) << std::setfill('0') << std::hex << (int)c;
				clean << c;

				if (i != length - 1)
					ss << ", ";
			}

			return std::make_tuple(ss.str(), clean.str());
		};

		size_t test_sizes[] = { 0, 32768, 65536, 131072 };

		for (int i = 0; i < std::size(test_sizes); i++)
		{
			auto random_string_value = random_string_array(test_sizes[i]);

			std::string script = R"(
			register((response, request) => {
			    response.write(new Uint8Array([)" + std::get<0>(random_string_value) + R"(]), 'text/html');

			    return RQ_NOTIFICATION_FINISH_REQUEST;
			});
			)";

			EXECUTE_SCRIPT(std::move(script));

			httplib::Client http_client(HOST);
			auto response = http_client.Get("/");

			if (!response) Assert::Fail(L"failed to get http response.");

			Assert::AreEqual(
				response->body == std::get<1>(random_string_value),
				true
			);
		}
	}
};
