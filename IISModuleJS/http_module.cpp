#include "http_module.h"
 
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" ) 

// An array containing all loaded js modules.
iis_module_js::IISModuleJS* modules[NUM_OF_MODULES];

// An index indicating the current engine to use.
std::atomic<int> module_index = 0;

/**
* HttpModule constructor.
*/
HttpModule::HttpModule()
{
	engine_id = INCREMENT_COUNT;
}

/**
 * OnBeginRequest
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnBeginRequest(IN IHttpContext* pHttpContext, IN IHttpEventProvider* pProvider)
{
	// Return processing to the pipeline. 
	return REQUEST_NOTIFICATION_STATUS(
		modules[engine_id]->handle_callback(iis_module_js::BEGIN_REQUEST, pHttpContext, pProvider)
	); 
}

/**
 * OnSendResponse
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnSendResponse(IN IHttpContext* pHttpContext, IN ISendResponseProvider* pProvider)
{
	// Return processing to the pipeline. 
	return REQUEST_NOTIFICATION_STATUS(
		modules[engine_id]->handle_callback(iis_module_js::SEND_RESPONSE, pHttpContext, pProvider)
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
		modules[INCREMENT_COUNT]->handle_callback(iis_module_js::PRE_BEGIN_REQUEST, pProvider->GetHttpContext(), pProvider)
	);
}