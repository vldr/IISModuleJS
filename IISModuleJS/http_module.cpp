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

/**
 * HttpModule
 */
HttpModule::HttpModule()
{
	// Open a handle to the Event Viewer.
	m_hEventLog = RegisterEventSource(NULL, "IISADMIN");
}

/**
 * ~HttpModule
 */
HttpModule::~HttpModule()
{
	// Test whether the handle for the Event Viewer is open.
	if (NULL != m_hEventLog)
	{
		// Close the handle to the Event Viewer.
		DeregisterEventSource(m_hEventLog);
		m_hEventLog = NULL;
	}
}