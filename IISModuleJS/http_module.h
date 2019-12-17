#pragma once
#include "v8_wrapper.h"

#pragma comment(lib, "Ws2_32.lib")
#include <icftypes.h>

class HttpModule : public CHttpModule
{
public:
	HttpModule();
	~HttpModule();

	REQUEST_NOTIFICATION_STATUS OnBeginRequest(
		IN IHttpContext* pHttpContext,
		IN IHttpEventProvider* pProvider
	);
private:
	HANDLE m_hEventLog;
};

