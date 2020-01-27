#include "http_module.h"
 
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" ) 

/**
 * OnBeginRequest
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
{
	// Return processing to the pipeline. 
	return REQUEST_NOTIFICATION_STATUS(
		v8_wrapper::handle_callback(v8_wrapper::BEGIN_REQUEST, pHttpContext, pProvider)
	);
}

/**
 * OnSendResponse
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnSendResponse(IN IHttpContext* pHttpContext, IN ISendResponseProvider* pProvider)
{
	// Return processing to the pipeline. 
	return REQUEST_NOTIFICATION_STATUS(
		v8_wrapper::handle_callback(v8_wrapper::SEND_RESPONSE, pHttpContext, pProvider)
	);
}

//////////////////////////////////////

/**
 * OnGlobalPreBeginRequest
 */
GLOBAL_NOTIFICATION_STATUS HttpGlobalModule::OnGlobalPreBeginRequest(IN IPreBeginRequestProvider* pProvider)
{
	// Return processing to the pipeline. 
	return GLOBAL_NOTIFICATION_STATUS(
		v8_wrapper::handle_callback(v8_wrapper::PRE_BEGIN_REQUEST, pProvider->GetHttpContext(), pProvider)
	);
}