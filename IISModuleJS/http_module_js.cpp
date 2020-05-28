#include "http_module_js.h"

namespace iis_module_js {
	v8pp::module HTTPModuleJS::get_global_fetch()
	{
		// http Property
		v8pp::module http_module(isolate);

		// http.fetch(
		//	   hostname: String,
		//	   path: String, 
		//	   init: Object {optional},
		// ): Promise
		http_module.set("fetch", [this](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 2) 
				throw std::exception("invalid function signature for http.fetch");

			///////////////////////////////////////////////
			
			if (!args[0]->IsString() || !args[1]->IsString()) 
				throw std::exception("invalid argument types for http.fetch");

			///////////////////////////////////////////////

			// Require Parameters
			
			auto hostname = v8pp::from_v8<std::string>(isolate, args[0]);
			auto path = v8pp::from_v8<std::string>(isolate, args[1]);

			///////////////////////////////////////////////

			FetchRequest fetch_request(std::move(hostname), std::move(path));

			///////////////////////////////////////////////
			
			if (args.Length() > 2 && args[2]->IsObject())
			{
				auto object = args[2].As<v8::Object>();
				
				////////////////////////////////////

				static const char* const kKeys[] = 
				{
					"body",
					"method",
					"is_ssl",
					"headers"
				};

				auto keys = m_iis_module_js->find_or_create_eternal_name_cache(
					kKeys, 
					kKeys, 
					std::size(kKeys)
				);

				////////////////////////////////////
				
				auto context = isolate->GetCurrentContext();
				
				////////////////////////////////////
				
				// "body" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[0].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsString())
					{
						fetch_request.body = v8pp::from_v8<std::string>(isolate, value);
					}
				}

				////////////////////////////////////

				// "method" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[1].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsString())
					{
						fetch_request.method = v8pp::from_v8<std::string>(isolate, value);
					}
				}

				////////////////////////////////////

				// "is_ssl" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[2].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsBoolean())
					{
						fetch_request.is_ssl = v8pp::from_v8<bool>(isolate, value);
					}
				}

				////////////////////////////////////

				// "headers" property //

				{
					v8::Local<v8::Value> value;

					if (!object->Get(context, keys[3].Get(isolate)).ToLocal(&value))
					{
						throw std::exception("unable to get value.");
					}

					if (value->IsObject())
					{
						auto object_value = value.As<v8::Object>();
					
						if (!object_value.IsEmpty())
						{
							auto prop_names = object_value->GetPropertyNames(context).ToLocalChecked();
							
							for (uint32_t i = 0, count = prop_names->Length(); i < count; ++i)
							{
								v8::Local<v8::Value> key = prop_names->Get(context, i).ToLocalChecked();
								v8::Local<v8::Value> val = object_value->Get(context, key).ToLocalChecked();

								fetch_request.headers.emplace(
									v8pp::from_v8<std::string>(isolate, key),
									v8pp::from_v8<std::string>(isolate, val)
								);
							}
						}
					}
				}
			} 

			/////////////////////////////////////////////

			// Wait until threads free up.
			m_iis_module_js->create_thread();

			/////////////////////////////////////////////

			// Setup a resolver.
			auto resolver = v8::Promise::Resolver::New(args.GetIsolate()->GetCurrentContext()).ToLocalChecked();
			auto resolver_global = v8::Global<v8::Promise::Resolver>(args.GetIsolate(), resolver);

			// Set the return value to our promise.
			args.GetReturnValue().Set(
				resolver_global.Get(isolate)->GetPromise()
			);

			// Our request thread.
			std::thread request_thread([this, 
				resolver = std::move(resolver_global), 
				fetch_request = std::move(fetch_request)
			] {
				std::unique_ptr<httplib::Response> response;
				 
				if (fetch_request.is_ssl)
				{
					httplib::SSLClient client(fetch_request.hostname);
					client.enable_server_certificate_verification(false);

					if (fetch_request.method == "GET")
					{
						response = client.Get(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "POST")
					{
						response = client.Post(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "HEAD")
					{
						response = client.Head(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PUT")
					{
						response = client.Put(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}
					
					if (fetch_request.method == "DELETE")
					{
						response = client.Delete(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "OPTIONS")
					{
						response = client.Options(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PATCH")
					{
						response = client.Patch(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body, nullptr);
					}
				}
				else
				{
					httplib::Client client(fetch_request.hostname);	

					if (fetch_request.method == "GET")
					{
						response = client.Get(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "POST")
					{
						response = client.Post(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "HEAD")
					{
						response = client.Head(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PUT")
					{
						response = client.Put(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "DELETE")
					{
						response = client.Delete(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body);
					}

					if (fetch_request.method == "OPTIONS")
					{
						response = client.Options(fetch_request.path.c_str(), fetch_request.headers);
					}

					if (fetch_request.method == "PATCH")
					{
						response = client.Patch(fetch_request.path.c_str(), fetch_request.headers, fetch_request.body, nullptr);
					}
				} 

				////////////////////////////////////////////

				// Decrement out thread count.
				m_iis_module_js->release_thread();
				
				////////////////////////////////////////////

				// We should only lock once the request has finished.
				v8::Locker locker(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Context::Scope context_scope(context.Get(isolate));

				// Check if our request was successful.
				if (!response)
				{
					resolver.Get(isolate)->Reject(
						isolate->GetCurrentContext(),
						v8pp::to_v8(isolate, "unable to fetch")
					);
					 
					return;  
				}

				//////////////////////////////////

				auto fetch_object = global_fetch_object.Get(isolate)->Clone();

				//////////////////////////////////

				auto fetch_response = new FetchResponse(isolate, fetch_object, response.release());

				//////////////////////////////////
				
				fetch_response->response_object.SetWeak(
					fetch_response,
					[](const v8::WeakCallbackInfo<FetchResponse>& data)
					{
						// Reset our JS object.
						data.GetParameter()->response_object.Reset();

						///////////////////////////////

						// Decrement our external memory usage.
						data.GetIsolate()->AdjustAmountOfExternalAllocatedMemory(
							-data.GetParameter()->capacity()
						);

						///////////////////////////////

						// Delete our object.
						delete data.GetParameter();
					},
					v8::WeakCallbackType::kParameter
				);

				// Increment our external memory usage.
				isolate->AdjustAmountOfExternalAllocatedMemory(
					fetch_response->capacity()
				);

				//////////////////////////////////

				resolver.Get(isolate)->Resolve(
					isolate->GetCurrentContext(), 
					fetch_object
				);
			});

			request_thread.detach();
		}
		
		);
	}

	v8pp::module HTTPModuleJS::get_fetch()
	{
		// Setup our module...
		v8pp::module module(isolate);

		// Setup our functions

		// status(): number
		module.set("status", [this](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for status");

			RETURN_THIS(FETCH_RESPONSE->status)
		});

		// text(): String || null
		module.set("text", [this](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for text");

			////////////////////////////////////////////////

			if (FETCH_RESPONSE->body.empty()) RETURN_NULL;

			////////////////////////////////////////////////  

			auto external_string = new ExternalString(FETCH_RESPONSE->body.size());

			//////////////////////////////////////////////// 

			std::memcpy(
				(void*)external_string->data(),
				FETCH_RESPONSE->body.data(),
				FETCH_RESPONSE->body.size()
			);

			////////////////////////////////////////////////

			auto string = v8::String::NewExternalOneByte(isolate, external_string);

			////////////////////////////////////////////////

			args.GetReturnValue().Set(
				string.ToLocalChecked()
			);
		});

		// blob(): Uint8Array || null
		module.set("blob", [this](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for blob");

			////////////////////////////////////////////////

			auto body = FETCH_RESPONSE->body;

			////////////////////////////////////////////////

			if (body.empty()) RETURN_NULL;

			////////////////////////////////////////////////

			auto array_buffer = v8::ArrayBuffer::New(
				isolate,
				body.size()
			);

			////////////////////////////////////////////////

			std::memcpy(
				array_buffer->GetContents().Data(),
				body.data(),
				body.size()
			);

			////////////////////////////////////////////////

			auto uint8_array = v8::Uint8Array::New(
				array_buffer,
				0,
				body.size()
			);

			////////////////////////////////////////////////

			args.GetReturnValue().Set(uint8_array);
		});

		// headers(): Object<String, String> || null
		module.set("headers", [this](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (!FETCH_RESPONSE) throw std::exception("invalid fetch response for headers");

			////////////////////////////////////////////////

			auto headers = FETCH_RESPONSE->headers;

			////////////////////////////////////////////////

			if (headers.empty()) RETURN_NULL;

			////////////////////////////////////////////////

			auto headers_object = v8::Object::New(isolate);

			////////////////////////////////////////////////

			for (auto header : headers)
			{
				headers_object->Set(
					isolate->GetCurrentContext(),
					v8pp::to_v8(isolate, header.first),
					v8pp::to_v8(isolate, header.second)
				);
			}

			////////////////////////////////////////////////

			args.GetReturnValue().Set(
				headers_object
			);
		});

		// Set our internal field count.
		module.obj_->SetInternalFieldCount(1);

		return module;
	}
}