#include "module_factory.h"

/**
 * RegisterModule
 */
HRESULT __stdcall RegisterModule(DWORD dwServerVersion, IHttpModuleRegistrationInfo * pModuleInfo, IHttpServer * pHttpServer)
{
	UNREFERENCED_PARAMETER(dwServerVersion);

	v8_wrapper::start(pHttpServer->GetAppPoolName());

	return pModuleInfo->SetRequestNotifications(new ModuleFactory(),
			RQ_BEGIN_REQUEST,
		0); 
} 