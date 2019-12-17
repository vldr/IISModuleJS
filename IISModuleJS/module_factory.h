#pragma once
#include "http_module.h"

class ModuleFactory : public IHttpModuleFactory
{
public:
	virtual HRESULT GetHttpModule(OUT CHttpModule ** ppModule, IN IModuleAllocator * pAllocator)
	{
		// Set our unreferenced param...
		UNREFERENCED_PARAMETER(pAllocator);

		// Create a new instance.
		HttpModule * pModule = new HttpModule;

		// Test for an error.
		if (!pModule)
		{
			// Return an error if the factory cannot create the instance.
			return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
		}
		else
		{
			// Return a pointer to the module.
			*ppModule = pModule;
			pModule = NULL;
			// Return a success status.
			return S_OK;
		}
	}

	virtual void Terminate()
	{
		// Remove the class from memory.
		delete this;
	}
};

