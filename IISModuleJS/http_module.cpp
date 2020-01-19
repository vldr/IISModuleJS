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
		v8_wrapper::handle_callback(v8_wrapper::BEGIN_REQUEST, pHttpContext)
	);
}

/**
 * OnSendResponse
 */
REQUEST_NOTIFICATION_STATUS HttpModule::OnSendResponse(IN IHttpContext* pHttpContext, IN ISendResponseProvider* pProvider)
{
	if (!v8_wrapper::is_registered(v8_wrapper::SEND_RESPONSE)) return RQ_NOTIFICATION_CONTINUE;

	//////////////////////////////////////////////
	
	auto http_response = pHttpContext->GetResponse();

	//////////////////////////////////////////////

	if (!http_response) return RQ_NOTIFICATION_CONTINUE;

	//////////////////////////////////////////////
	
	auto flags = pProvider->GetFlags();

	////////////////////////////////////////////////

	auto chunk_count = http_response->GetRawHttpResponse()->EntityChunkCount;

	////////////////////////////////////////////////

	for (int i = 0; i < chunk_count; i++)
	{
		auto chunk = http_response->GetRawHttpResponse()->pEntityChunks[i];

		//////////////////////// 

		if (chunk.DataChunkType != HttpDataChunkFromMemory) continue;
		if (!chunk.FromMemory.BufferLength) continue;

		////////////////////////

		if (m_response_chunks.m_number_of_chunks >= v8_wrapper::ResponseChunks::MAX_CHUNKS) 
			continue;

		//////////////////////// 

		m_response_chunks.m_chunks[m_response_chunks.m_number_of_chunks]
			= chunk.FromMemory.pBuffer;

		m_response_chunks.m_chunk_sizes[m_response_chunks.m_number_of_chunks]
			= chunk.FromMemory.BufferLength;

		///////////////////////

		m_response_chunks.m_total_size += chunk.FromMemory.BufferLength;
		m_response_chunks.m_number_of_chunks++;
	}

	////////////////////////////////////////////////
	
	if (flags == 0)
	{
		////////////////////////

		auto result = REQUEST_NOTIFICATION_STATUS(
			v8_wrapper::handle_callback(v8_wrapper::SEND_RESPONSE, pHttpContext, &m_response_chunks)
		);
		
		////////////////////////
		
		m_response_chunks.m_number_of_chunks = 0;
		m_response_chunks.m_total_size = 0;

		////////////////////////

		return result;
	}

	////////////////////////////////////////////////

	return RQ_NOTIFICATION_CONTINUE;
}

//////////////////////////////////////

/**
 * OnGlobalPreBeginRequest
 */
GLOBAL_NOTIFICATION_STATUS HttpGlobalModule::OnGlobalPreBeginRequest(IN IPreBeginRequestProvider* pProvider)
{
	// Return processing to the pipeline. 
	return GLOBAL_NOTIFICATION_STATUS(
		v8_wrapper::handle_callback(v8_wrapper::PRE_BEGIN_REQUEST, pProvider->GetHttpContext())
	);
}