#pragma once
#include "iis_module_js.h"

#pragma comment(lib, "Ws2_32.lib")
#include <icftypes.h>

extern iis_module_js::IISModuleJS * modules[NUM_OF_MODULES];
extern std::atomic<int> module_index;

class HttpModule : public CHttpModule
{

public:
	HttpModule();

	REQUEST_NOTIFICATION_STATUS OnBeginRequest(
		IN IHttpContext* pHttpContext,
		IN IHttpEventProvider* pProvider
	);

	REQUEST_NOTIFICATION_STATUS OnSendResponse(
		_In_ IHttpContext* pHttpContext,
		_In_ ISendResponseProvider* pProvider
	);
private:
	int engine_id = 0;
};

class HttpGlobalModule : public CGlobalModule
{
public:
	GLOBAL_NOTIFICATION_STATUS OnGlobalPreBeginRequest(IN IPreBeginRequestProvider* pProvider);
	
	VOID Terminate()
	{
		delete this;
	}
};

