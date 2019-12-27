<img src="https://i.imgur.com/JpPLWOC.png" />
A middleware for IIS (Internet Information Services) that allows you to harness the power and convenience of JavaScript for each request.

# 

# Dependencies
* [v8](https://github.com/v8/v8)
* [rpclib](https://github.com/rpclib/rpclib)
* [openssl](https://github.com/openssl/openssl)
* [httplib](https://github.com/yhirose/cpp-httplib) (included)
* [v8pp](https://github.com/pmed/v8pp) (included)
* [simdb](https://github.com/LiveAsynchronousVisualizedArchitecture/simdb) (included)

# Getting Started
### Installation
1. Download the precompiled dynamic-link library from the releases page.
2. Follow the instructions given [here](https://docs.microsoft.com/en-us/iis/develop/runtime-extensibility/develop-a-native-cc-module-for-iis#deploying-a-native-module) to install the dynamic-link library in IIS.
### Running Scripts
All scripts are executed from the `%PUBLIC%` directory. The module watches each script for changes and dynamically reloads a script if a change was found. 

Scripts be should named with their corresponding [application pool name](https://blogs.msdn.microsoft.com/rohithrajan/2017/10/08/quick-reference-iis-application-pool/). For example, the site `vldr.org` would likely have the application pool name `vldr_org` thus the script should be named `vldr_org.js`

You can load as many subsequent scripts as you want using the *load* function.

# API
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
You can only register one callback.

```javascript
function callback(response, request) 
{
    return RQ_NOTIFICATION_CONTINUE;
}

register(callback);
```

#### load(fileName: String, ...): void
Loads a script using *fileName* as the name of the JavaScript file.

```javascript
load("test.js");
```

#### print(msg: String, ...): void
Prints *msg* using OutputDebugString. You can observe the print out using a debugger or [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).
```javascript
print("test message");
```
