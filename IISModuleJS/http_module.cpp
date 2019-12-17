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

	// Check if our http response and request exists...
	if (!pHttpResponse || !pHttpRequest) return RQ_NOTIFICATION_CONTINUE;

	/////////////////////////////////////

	v8_wrapper::begin_request(pHttpResponse, pHttpRequest);

	// Create an HRESULT to receive return values from methods.
	/*HRESULT hr;

	// Create an array of data chunks.
	HTTP_DATA_CHUNK dataChunk[2];

	// Buffer for bytes written of data chunk.
	DWORD cbSent;

	// Create string buffers.
	PCSTR pszOne = "First chunk data\n";
	PCSTR pszTwo = "Second chunk data\n";

	// Test for an error. 
	if (pHttpResponse != NULL)
	{
		// Clear the existing response.
		pHttpResponse->Clear();
		// Set the MIME type to plain text.
		pHttpResponse->SetHeader(
			HttpHeaderContentType, "text/plain",
			(USHORT)strlen("text/plain"), TRUE);

		// Set the chunk to a chunk in memory.
		dataChunk[0].DataChunkType = HttpDataChunkFromMemory;
		// Set the chunk to the first buffer.
		dataChunk[0].FromMemory.pBuffer = (PVOID)pszOne;
		// Set the chunk size to the first buffer size.
		dataChunk[0].FromMemory.BufferLength = (USHORT)strlen(pszOne);

		// Set the chunk to a chunk in memory.
		dataChunk[1].DataChunkType = HttpDataChunkFromMemory;
		// Set the chunk to the second buffer.
		dataChunk[1].FromMemory.pBuffer =
			(PVOID)pszTwo;
		// Set the chunk size to the second buffer size.
		dataChunk[1].FromMemory.BufferLength =
			(USHORT)strlen(pszTwo);

		// Insert the data chunks into the response.
		hr = pHttpResponse->WriteEntityChunks(
			dataChunk, 2, FALSE, TRUE, &cbSent);

		// Test for an error.
		if (FAILED(hr))
		{
			// Set the error status.
			pProvider->SetErrorStatus(hr);
		}
		// End additional processing.
		return RQ_NOTIFICATION_FINISH_REQUEST;
	}*/

	// Return processing to the pipeline.
	return RQ_NOTIFICATION_CONTINUE;
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