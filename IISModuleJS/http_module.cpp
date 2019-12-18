#include "http_module.h"
 
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" )


/**
 * OnBeginRequest
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
{
	// Retrieve a pointer to the response.
	IHttpResponse * pHttpResponse = pHttpContext->GetResponse();
	IHttpRequest * pHttpRequest = pHttpContext->GetRequest();


	/////////////////////////////////////

	// Return processing to the pipeline.
	return v8_wrapper::begin_request(pHttpResponse, pHttpRequest);
}