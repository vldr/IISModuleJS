<img src="https://i.imgur.com/JpPLWOC.png" />
A middleware for IIS that allows you to harness the power and convenience of JavaScript for each request. 
Only supports the begin request callback for now, make an issue if you want me to implement it. 

# 

## Dependencies
* [v8](https://github.com/v8/v8)
* [rpclib](https://github.com/rpclib/rpclib)

## Installation
1. Download the precompiled dynamic link library in the releases page.
2. Follow the instructions given [here.](https://docs.microsoft.com/en-us/iis/develop/runtime-extensibility/develop-a-native-cc-module-for-iis#deploying-a-native-module)

## API
#### register(callback: (Function(Response, Request): REQUEST_NOTIFICATION_STATUS)): void
Registers the callback for begin request.
Your callback function will be provided a request object representing IHttpRequest, and a response object representing IHttpResponse.
You can only register one callback per worker.

```javascript
function callback(response, request) 
{
    return RQ_NOTIFICATION_CONTINUE;
}

register(callback);
```
