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
#### REQUEST_NOTIFICATION_STATUS: enum
The members of the REQUEST_NOTIFICATION_STATUS enumeration are used as return values from request-level notifications, and the members help to control process flow within the integrated request-processing pipeline.[⁺](https://docs.microsoft.com/en-us/iis/web-development-reference/native-code-api-reference/request-notification-status-enumeration#remarks)

* Use *RQ_NOTIFICATION_CONTINUE* if you want the request continue to other modules and rest of the pipeline.
* Use *RQ_NOTIFICATION_FINISH_REQUEST* if you want the request to be handled by you.
```javascript
enum REQUEST_NOTIFICATION_STATUS {
    // continue processing for notification
    RQ_NOTIFICATION_CONTINUE = 0,
    
    // suspend processing for notification (DO NOT USE FOR INTERNAL USE ONLY!)
    RQ_NOTIFICATION_PENDING = 1, 
    
    // finish request processing
    RQ_NOTIFICATION_FINISH_REQUEST = 2 
}
```

#### register(callback: (Function(Response, Request): REQUEST_NOTIFICATION_STATUS)): void
Registers a given function as a callback which will be called for every request.

Your callback function will be provided a Request object, and a Response object for the given request.
You can only register one callback per worker.

```javascript
function callback(response, request) 
{
    return RQ_NOTIFICATION_CONTINUE;
}

register(callback);
```
