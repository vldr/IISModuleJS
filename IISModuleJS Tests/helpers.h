#pragma once
#include <httplib/httplib.h>
#include <rpc/client.h>
#pragma comment(lib, "rpc.lib")

#define HOST "127.0.0.1"
#define IPV6_HOST "[::1]"
#define EXECUTE_SCRIPT(script) rpc::client rpc_client("127.0.0.1", 8080); \
auto result = rpc_client.call("execute", std::string(script)).as<bool>(); \
Assert::AreEqual(result, true);