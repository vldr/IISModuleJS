<img src="https://i.imgur.com/JpPLWOC.png" />
A middleware for IIS (Internet Information Services) that allows you to harness the power and convenience of JavaScript for each request.

# 

# Dependencies
* [v8](https://github.com/v8/v8) (included precompiled library file in releases)
* [rpclib](https://github.com/rpclib/rpclib) (included precompiled library file in releases)
* [openssl](https://github.com/openssl/openssl) (included precompiled library file in releases)
* [httplib](https://github.com/yhirose/cpp-httplib) 
* [v8pp](https://github.com/pmed/v8pp) 
* [simdb](https://github.com/LiveAsynchronousVisualizedArchitecture/simdb)

# Getting Started
### Installation
1. Download *iismodulejs.64.dll* from the releases page.
2. Follow the instructions given [here](https://docs.microsoft.com/en-us/iis/develop/runtime-extensibility/develop-a-native-cc-module-for-iis#deploying-a-native-module) to install the dynamic-link library in IIS.
### Running Scripts
All scripts are executed from the `%PUBLIC%` directory. The module watches each script for changes and dynamically reloads a script if a change was found. 

Scripts should be named with their corresponding [application pool name](https://blogs.msdn.microsoft.com/rohithrajan/2017/10/08/quick-reference-iis-application-pool/). For example, the site `vldr.org` would likely have the application pool name `vldr_org` thus the script should be named `vldr_org.js`

You can load as many subsequent scripts as you want using the [load](#loadfilename-string--void) function.

# API
#### REQUEST_NOTIFICATION_STATUS: enum
The members of the REQUEST_NOTIFICATION_STATUS enumeration are used as return values from request-level notifications, and the members help to control process flow within the integrated request-processing pipeline.[⁺](https://docs.microsoft.com/en-us/iis/web-development-reference/native-code-api-reference/request-notification-status-enumeration#remarks)

* Use *RQ_NOTIFICATION_CONTINUE* if you want the request to continue to other modules and the rest of the pipeline.
* Use *RQ_NOTIFICATION_FINISH_REQUEST* if you want the request to be handled only by yourself.
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

Your callback function will be provided a [Response](#response-object) object, and a [Request](#request-object) object respectively. The callback function can also be asynchronous but keep in mind that this will yield far **worse** performance than using an ordinary function.

```javascript
function callback(response, request) 
{
    return RQ_NOTIFICATION_CONTINUE;
}

register(callback);
```

#### load(fileName: String, ...): void
Loads a script using **fileName** as the name of the JavaScript file, the name should include the extension.

```javascript
// Loads a single script.
load("script.js");

// Load multiple scripts.
load("script.js", "script2.js");
```

#### print(msg: String, ...): void
Prints **msg** using OutputDebugString. You can observe the print out using a debugger or [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).
```javascript
// Prints "test message."
print("test message");

// Prints "test message and then some."
print("test message", "and then some");
```

### IPC: Interface
The interprocess communication interface provides a key-value store where you can share JavaScript data across different processes/workers.

#### ipc.set(key: String, value: any): void
Sets a **key** with a given **value**.

```javascript
register((response, request) => 
{
    // Get our client's ip address.
    const ip = request.getRemoteAddress();
    
    // An example object.
    const object = { 
        number: 3.14,
        text: "sample text",
        array: [ 3.14, "sample text" ]
    };
    
    // Set our key-value.
    ipc.set(
        ip, 
        object
    ); 
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### ipc.get(key: String): any || null
Returns a value with the corresponding **key**. 
This function will return *null* if the key does not exist, make sure to check for it.

```javascript
register((response, request) => 
{
    // Get our client's ip address.
    const ip = request.getRemoteAddress();
    
    // Get our value.
    const value = ipc.get(ip);
    
    // Check if our value exists.
    if (value)
    {
        // Will print out our value.
        print(
            JSON.stringify(
                value
            )
        );
    }
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

### HTTP: Interface

##### http.fetch(hostname: String, path: String, isSSL: bool, method: String {optional}, params: Object<String, String> {optional}): Promise<{ body: String, status: Number }, String>
This function only supports POST and GET methods and should **not** be mistaken for the [fetch](https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API) found in the standard web api.

The **hostname** parameter specifies the domain name of the server. 

The **path** parameter specifies the path part of the HTTP request. 

The **method** parameter specifies the method part of the HTTP request, only, GET and POST.

The **params** parameter should be an Object (not an Array) which will represent the collection of key-value POST parameters (imagine an HTML form) for the HTTP POST request.

The example below requires some familiarity with [Promises](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Using_promises). This example strictly demonstrates how an async callback can be used, but you can mix async and non-async functions depending on your needs.
```javascript
register(
    // Notice how we're using an asynchronous callback,
    // this is because we'd like the response from our http.fetch
    // to be written to the request's body as soon it arrives (using await).
    async (response, request) => 
    {
        // Attempts to fetch the latest comic from xkcd, 
        // notice how our isSSL is set to true since the api endpoint
        // is HTTPS.
        await http.fetch("xkcd.com", "/info.0.json", true)
            .then((reply) => {
                // Write our response using the reply body.
                response.write(reply.body, "application/json");
            })
            .catch((error) => {
                // Write our error as the response.
                response.write("An error occurred while fetching.", "text/html");
            });
    
        // Finish our request since we want our response to be written.
        return RQ_NOTIFICATION_FINISH_REQUEST
    }
);
```

### Request: Object

#### read(rewrite: bool {optional}): String || null
Returns the HTTP request body as a String. The **rewrite** parameter determines whether to rewrite all the data back into the pipeline; set this to *true* if you expect to let the request continue to PHP or any other module which will use the request data. 

**Note:** Setting rewrite to *true* will yield far worse performance.

```javascript
register((response, request) => 
{
    // This reads the request body.
    const body = request.read(); 
    
    // Prints out the body.
    print(
        body
    );

    // You have to use RQ_NOTIFICATION_FINISH_REQUEST because 
    // once you read the request data then this module takes the responsibility
    // of handling the request entirely, otherwise modules in 
    // the pipeline wouldn't be able to read any request data since WE 
    // read it already.
    return RQ_NOTIFICATION_FINISH_REQUEST;
});
```

#### getHost(): String
Returns the host section of the URL for the request.

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "127.0.0.1:80"
    print(
        request.getHost()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getQueryString(): String
Returns the query string for the request. 

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "?this=is&a=query&string"
    print(
        request.getQueryString()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getFullUrl(): String
Returns the full url for the request. 

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "http://127.0.0.1:80/this/is/a/absolute/path?this=is&a=query&string"
    print(
        request.getFullUrl()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getAbsPath(): String
Returns the absolute path for the request. 

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "/this/is/a/absolute/path"
    print(
        request.getAbsPath()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getMethod(): String
Returns the HTTP method for the current request. Example: GET, POST, etc.
```javascript
register((response, request) => 
{
    // Prints out the HTTP method.
    print(
        request.getMethod()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getLocalAddress(): String
Returns the address of the local interface for the current request. This will return either a IPv4 or IPv6 address.
```javascript
register((response, request) => 
{
    // Prints out the local IP address.
    print(
        request.getLocalAddress()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getRemoteAddress(): String
Returns the address of the local interface for the current request. This will return either an IPv4 or IPv6 address.
This method can be used to get the connecting client's IP address.
```javascript
register((response, request) => 
{
    // Prints out the remote IP address.
    print(
        request.getRemoteAddress()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getHeader(headerName: String): String || null
Returns the value of a specified HTTP header. 
If the header doesn't exist this function will return *null* so make sure to check for it.

```javascript
register((response, request) => 
{
    // Gets the value of the User-Agent header.
    const userAgent = request.getHeader('User-Agent');
    
    // Check if our header exists.
    if (userAgent)
        // Prints out for example "Mozilla/5.0 (iPhone; CPU iPhone OS 12_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/12.0 Mobile/15E148 Safari/604.1"
        print(userAgent);
   
    return RQ_NOTIFICATION_CONTINUE;
});
```

### Response: Object

#### write(body: String || Uint8Array, mimeType: String, contentEncoding: String {optional}): void
The **body** parameter gets written to the response. 
The **mimeType** parameter sets the [Content-Type](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type) header with the given value.
The **contentEncoding** parameter sets the [Content-Encoding](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding) header so you can provide compressed data through a response. 

The example below demonstrates various ways of writing *"hi"* as a response:
```javascript
register((response, request) => 
{
    // Writes a response with a String.
    response.write("hi", "text/html");
    
    // Writes a response with a Uint8Array.
    response.write(new Uint8Array([0x68, 0x69]), "text/html");
    
    // Writes a response with a Uint8Array and using the deflate content encoding.
    response.write(
        new Uint8Array([0x78, 0x9c, 0xcb, 0xc8, 0x04, 0x00, 0x01, 0x3b, 0x00, 0xd2]), 
        "text/html", 
        "deflate"
    );
    
    // You have to use RQ_NOTIFICATION_FINISH_REQUEST because 
    // we want the request to finish here, and not 
    // continue down the IIS pipeline; otherwise our written
    // response will be overwritten by other modules like the static file
    // module.
    return RQ_NOTIFICATION_FINISH_REQUEST;
});
```

#### setHeader(headerName: String, headerValue: String, shouldReplace: bool {optional}): bool
Sets or appends the value of a specified HTTP response header. 

The **headerName** parameter defines the name of the header, example: "Content-Type."
The **headerValue** parameter sets the value of the header, example: "text/html."
The **shouldReplace** parameter determines whether to replace the value of a preexisting header or to append to it. 

```javascript
register((response, request) => 
{
    // This will replace the 'Server' header with the value 'custom server value'
    response.setHeader('Server', 'custom server value');

    // This will append 'custom server value 2' to the 'Server' header.
    response.setHeader('Server', 'custom server value 2', false);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getHeader(headerName: String): String || null
Returns the value of a specified HTTP header. 
If the header doesn't exist this function will return *null* so make sure to check for it.

```javascript
register((response, request) => 
{
    // Gets the value of the server header.
    const serverHeaderValue = response.getHeader('Server');
    
    // Check if our header exists, in this case, it will.
    if (serverHeaderValue)
        // Prints out "Microsoft-IIS/10.0" (depending on your server version)
        print(serverHeaderValue);
   
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### clear(): void
Clears the response body. 
```javascript
register((response, request) => 
{
    response.clear();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### clearHeaders(): void
Clears the response headers and sets headers to default values.
```javascript
register((response, request) => 
{
    response.clearHeaders();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### closeConnection(): void
Closes the connection and sends a reset packet to the client.
```javascript
register((response, request) => 
{
    response.closeConnection();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### setNeedDisconnect(): void
Resets the socket after the response is complete.
```javascript
register((response, request) => 
{
    response.setNeedDisconnect();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getKernelCacheEnabled(): bool
Determines whether the kernel cache is enabled for the current response.
```javascript
register((response, request) => 
{
    response.getKernelCacheEnabled();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### resetConnection(): void
Resets the socket connection immediately.
```javascript
register((response, request) => 
{
    response.resetConnection();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getStatus(): Number
Retrieves the HTTP status for the response.

```javascript
register((response, request) => 
{
    const status = response.getStatus();
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### redirect(url: String, resetStatusCode: bool, includeParameters: bool): bool
Redirects the client to a specified URL.

```javascript
register((response, request) => 
{
    response.redirect("/location", true /* resetStatusCode */, true /* includeParameters */);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### setErrorDescription(decription: String, shouldHtmlEncode: bool): bool
Specifies the custom error description.

```javascript
register((response, request) => 
{
    response.setErrorDescription("error <b>description</b>", true /* shouldHtmlEncode */);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### disableKernelCache(reason: Number): void
Disables the kernel cache for this response.

```javascript
register((response, request) => 
{
    const HANDLER_HTTPSYS_UNFRIENDLY = 9;
    
    response.setErrorDescription(HANDLER_HTTPSYS_UNFRIENDLY);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```


#### deleteHeader(headerName: String): bool
Deletes an HTTP header from the request.

```javascript
register((response, request) => 
{
    response.deleteHeader('Server');

    return RQ_NOTIFICATION_CONTINUE;
});
```
