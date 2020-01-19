#pragma once
#include "v8_wrapper.h"

#pragma comment(lib, "Ws2_32.lib")
#include <icftypes.h>

class HttpModule : public CHttpModule
{
public:
	REQUEST_NOTIFICATION_STATUS OnBeginRequest(
		IN IHttpContext* pHttpContext,
		IN IHttpEventProvider* pProvider
	);

	REQUEST_NOTIFICATION_STATUS OnSendResponse(
		_In_ IHttpContext* pHttpContext,
		_In_ ISendResponseProvider* pProvider
	);

private:
	v8_wrapper::ResponseChunks m_response_chunks
		= v8_wrapper::ResponseChunks();
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

