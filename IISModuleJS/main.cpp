#include "module_factory.h"

/**
 * RegisterModule
 */
HRESULT WINAPI RegisterModule(DWORD dwServerVersion, IHttpModuleRegistrationInfo * pModuleInfo, IHttpServer * pHttpServer)
{
	UNREFERENCED_PARAMETER(dwServerVersion);

	//////////////////////////////////////
	
	v8_wrapper::start(pHttpServer->GetAppPoolName());

	//////////////////////////////////////
	
	auto hr = pModuleInfo->SetGlobalNotifications(new HttpGlobalModule(), GL_PRE_BEGIN_REQUEST);

	//////////////////////////////////////
	
	if (FAILED(hr)) return hr;

	//////////////////////////////////////
	
	return pModuleInfo->SetRequestNotifications(new ModuleFactory(), RQ_BEGIN_REQUEST | RQ_SEND_RESPONSE, 0);
} 