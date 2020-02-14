<img src="https://i.imgur.com/JpPLWOC.png" />
A middleware for IIS (Internet Information Services) that allows you to harness the power and convenience of JavaScript for each request.

# 

# Dependencies
* [v8](https://github.com/v8/v8) (included precompiled library file in [releases](../../releases))
* [rpclib](https://github.com/rpclib/rpclib) (included precompiled library file in [releases](../../releases))
* [openssl](https://github.com/openssl/openssl) (included precompiled library file in [releases](../../releases))
* [httplib](https://github.com/yhirose/cpp-httplib) 
* [v8pp](https://github.com/pmed/v8pp) 
* [simdb](https://github.com/LiveAsynchronousVisualizedArchitecture/simdb)

# Getting Started
### Installation
1. Download *iismodulejs.64.dll* from the [releases](../../releases) page.
2. Follow the instructions given [here](https://docs.microsoft.com/en-us/iis/develop/runtime-extensibility/develop-a-native-cc-module-for-iis#deploying-a-native-module) to install the dynamic-link library in IIS.
### Running Scripts
All scripts are executed from the `%PUBLIC%` directory. The module watches each script for changes and dynamically reloads a script if a change was found. 

Scripts should be named with their corresponding [application pool name](https://blogs.msdn.microsoft.com/rohithrajan/2017/10/08/quick-reference-iis-application-pool/). For example, the site `vldr.org` would likely have the application pool name `vldr_org` thus the script should be named `vldr_org.js`

You can load as many subsequent scripts as you want using the [load](#loadfilename-string--void) function.

# API
#### Pipeline Object
The members of the Pipeline enumeration are used as return values from request-level notifications, and the members help to control process flow within the integrated request-processing pipeline.[⁺](https://docs.microsoft.com/en-us/iis/web-development-reference/native-code-api-reference/request-notification-status-enumeration#remarks)

* Use *CONTINUE* if you want the request to continue to other modules and the rest of the pipeline.
* Use *FINISH* if you want the request to be handled only by yourself.

```ts
enum PIPELINE {
    // Continue processing to other modules (static file, rewrite, etc).
    CONTINUE = 0,

    // Finish request processing.
    FINISH = 1 
}
```

#

#### FetchRequestInit Object
This object represents the initialization information for the http.fetch function call.
Enables to set any custom settings that you want to apply to the request.

The **body** accessor is the body of the request.<br>
The **method** accessor is the request method, e.g., GET, POST. <br>
The **is_ssl** accessor represents whether the request is connecting to a TLS enabled endpoint.<br>
The **headers** accessor represents a key-value object (ordinary JS object) which will be used as the headers for the request.

```ts
interface FetchRequestInit {
    body?: string,
    method?: number,
    is_ssl?: boolean,
    headers?: string[][]
}
```



#### FetchResponse Object
This object represents the response from the http.fetch function call. 

The **status()** method returns the status code of the response.<br>
The **text()** method returns the response body as a **string** if it exists otherwise **null**.<br>
The **blob()** method returns the response body as a **Uint8Array** if it exists otherwise **null**.<br>
The **headers()** method returns the response headers as an **Object<String, String>**.

```ts
interface FetchResponse {
    status(): number,
    text(): string | null,
    blob(): Uint8Array | null,
    headers(): Object<String, String>
}
```

#

### **Register**

```ts
register(
    callbackType: number,
    callback?: (response: IISResponse, request: IISRequest, flag: number) 
    => PIPELINE | Promise<PIPELINE>
): void
```

Registers a given function as a callback which will be called for every request.

Your callback function will be provided a [Response](#response) object, and a [Request](#request) object respectively. The callback function can also be asynchronous but keep in mind that this will yield far **worse** performance than using an synchronous function.

There types of **callbackType**:

**BEGIN_REQUEST:**
<p>
This is the default callback and should be used in most cases. All features will work except <b>response.read</b> and <b>request.setUrl</b>. </p>

**SEND_RESPONSE:**
<p>
This callback occurs when IIS sends the response buffer. This means if you want to read the response body after it has been written to then this callback must be used. </p>

**PRE_BEGIN_REQUEST:**
<p>
This callback should be used to achieve high performance filtering and processing. Many features might not working in this callback, and some features will only work in this callback like <b>request.setUrl</b>. </p>



**Example:**
```javascript
function callback(response, request) 
{
    return CONTINUE;
}

register(callback);
```

#

### **Load**

```ts
load(...fileName: string[]): void
```
Loads a script using **fileName** as the name of the JavaScript file, the name should include the extension.

**Example:**

```javascript
// Loads a single script.
load("script.js");

// Load multiple scripts.
load("script.js", "script2.js");
```

#

### **Print**

```ts
print(...msg: string[]): void
```
Prints **msg** using OutputDebugString. You can observe the print out using a debugger or [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).

**Example:**
```javascript
// Prints "test message."
print("test message");

// Prints "test message and then some."
print("test message", "and then some");
```


## IPC
The interprocess communication interface provides a key-value store where you can share JavaScript data across different processes/workers.

### **Set**

```ts
ipc.set(key: string, value: any): void
```

Sets a **key** with a given **value**.

**Example:**
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
    
    return CONTINUE;
});
```

#

### **Get**

```ts
ipc.get(key: string): any | null
```
Returns a value with the corresponding **key**.

This function will return *null* if the key does not exist, make sure to check for it.

**Example:**

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
    
    return CONTINUE;
});
```


## HTTP

### **Fetch**

```ts
fetch(
    hostname: string, 
    path: string, 
    init?: FetchRequestInit
): Promise<FetchResponse>
```

This function provides an interface for fetching resources. This function (while similar) should **not** be mistaken for the [fetch](https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API) found in the standard web api.

The **hostname** parameter specifies the domain name of the server.<br>
The **path** parameter specifies the path part of the HTTP request. <br>
The **init** parameter specifies the [FetchRequestInit](#FetchRequestInit-Object) object which is optional.

The function returns a Promise which will yield a [FetchResponse](#FetchResponse-Object)

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
        await http.fetch("xkcd.com", "/info.0.json", { is_ssl: true })
            .then((reply) => {
                // Write our response using the reply body.
                response.write(reply.body, "application/json");
            })
            .catch((error) => {
                // Write our error as the response.
                response.write("An error occurred while fetching.", "text/html");
            });
    
        // Finish our request since we want our response to be written.
        return FINISH;
    }
);
```

## Request

### **Read**

```ts
read(rewrite?: boolean): void
```

Returns the HTTP request body as a String. 

The **rewrite** parameter determines whether to rewrite all the data back into the pipeline; set this to *true* if you expect to let the request continue to PHP or any other module which will use the request data. 

**Note:** Setting rewrite to *true* will yield far worse performance.

**Example:**

```javascript
register((response, request) => 
{
    // Reads the request body with rewrite set to true.
    let body = request.read(true); 
    
    // Prints out the body.
    print(
        body
    );
    
    ////////////////////////////////////
    
    // Reads the request body with rewrite set to false (default).
    body = request.read(); 
    
    // Prints out the body.
    print(
        body
    );
    
    ////////////////////////////////////
    
    // Reads the request body once again but since we didn't rewrite the 
    // request body back into the pipeline our result will be null.
    body = request.read(); 
    
    // Prints out 'null'.
    print(
        body
    );

    // End the request here. 
    return FINISH;
});
```
#

### **SetURL**

```ts
setUrl(url: string, resetQuerystring?: boolean): void
```
Set a new URL for the request. Can be used to rewrite urls but is **not** recommended.

The **url** parameter specifies to which path to rewrite the request to.<br>
The **resetQuerystring** parameter specifies whether to reset query string(s) associated with the request (erase them entirely).

**Example:**
```javascript
register((response, request) => 
{
    request.setUrl("/example", false);
    
    return CONTINUE;
});
```

#

### **SetHeader**

```ts
setHeader(headerName: string, headerValue: string, shouldReplace?: boolean): void
```
Sets or appends the value of a specified HTTP request header. 

The **headerName** parameter defines the name of the header, example: "Content-Type."<br>
The **headerValue** parameter sets the value of the header, example: "text/html."<br>
The **shouldReplace** parameter determines whether to replace the value of a preexisting header or to append to it. 

**Example:**

```javascript
register((response, request) => 
{
    // This will replace the 'User-Agent' header with the value 'custom server value'
    request.setHeader('User-Agent', 'custom server value');

    // This will append 'custom server value 2' to the 'User-Agent' header.
    request.setHeader('User-Agent', 'custom server value 2', false);
    
    return CONTINUE;
});
```
#

### **GetHeader**

```ts 
getHeader(headerName: string): string | null
```

Returns the value of a specified HTTP header. 
If the header doesn't exist this function will return *null* so make sure to check for it.


**Example:**

```javascript
register((response, request) => 
{
    // Gets the value of the User-Agent header.
    const userAgent = request.getHeader('User-Agent');
    
    // Check if our header exists.
    if (userAgent)
        // Prints out for example "Mozilla/5.0 (iPhone; CPU iPhone OS 12_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/12.0 Mobile/15E148 Safari/604.1"
        print(userAgent);
   
    return CONTINUE;
});
```
#

### **DeleteHeader**

```ts 
deleteHeader(headerName: String): void
```
Deletes an HTTP header from the request.

**Example:**

```javascript
register((response, request) => 
{
    request.deleteHeader('Server');

    return CONTINUE;
});
```
#

### **GetHost**

```ts 
getHost(): String
```
Returns the host section of the URL for the request.

**Example:**

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "127.0.0.1:80"
    print(
        request.getHost()
    );

    return CONTINUE;
});
```
#


### **GetQueryString**

```ts 
getQueryString(): String
```
Returns the query string for the request. 

**Example:**

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "?this=is&a=query&string"
    print(
        request.getQueryString()
    );

    return CONTINUE;
});
```
#

### **GetFullUrl**

```ts 
getFullUrl(): String
```
Returns the full url for the request. 

**Example:**

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "http://127.0.0.1:80/this/is/a/absolute/path?this=is&a=query&string"
    print(
        request.getFullUrl()
    );

    return CONTINUE;
});
```
#

### **GetAbsPath**

```ts 
getAbsPath(): String
```
Returns the absolute path for the request. 

**Example:**

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "/this/is/a/absolute/path"
    print(
        request.getAbsPath()
    );

    return CONTINUE;
});
```

#

### **GetMethod**

```ts 
getMethod(): String
```

Returns the HTTP method for the current request. Example: GET, POST, etc.

**Example:**
```javascript
register((response, request) => 
{
    // Prints out the HTTP method.
    print(
        request.getMethod()
    );

    return CONTINUE;
});
```
#

### **GetLocalAddress**

```ts 
getLocalAddress(): String
```

Returns the address of the local interface for the current request. This will return either a IPv4 or IPv6 address.

**Example:**
```javascript
register((response, request) => 
{
    // Prints out the local IP address.
    print(
        request.getLocalAddress()
    );

    return CONTINUE;
});
```

#

### **GetRemoteAddress**

```ts 
getRemoteAddress(): String
```

Returns the address of the remote interface for the current request. This will return either an IPv4 or IPv6 address.
<br>*This method can be used to get the connecting client's IP address.*

**Example:**
```javascript
register((response, request) => 
{
    // Prints out the remote IP address.
    print(
        request.getRemoteAddress()
    );

    return CONTINUE;
});
```

#

## Response

### **Write**

```ts
write(
    body: string | Uint8Array, 
    mimeType?: string, 
    contentEncoding?: string
): void
```

The **body** parameter gets written to the response. <br>
The **mimeType** parameter sets the [Content-Type](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type) header with the given value.<br>
The **contentEncoding** parameter sets the [Content-Encoding](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding) header so you can provide compressed data through a response. 

**Example:**

The example demonstrates various ways of writing *"hi"* as a response:
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
    
    // You have to use FINISH because 
    // we want the request to finish here, and not 
    // continue down the IIS pipeline; otherwise our written
    // response will be overwritten by other modules like the static file
    // module.
    return FINISH;
});
```
#

### **SetHeader**

```ts
setHeader(headerName: string, headerValue: string, shouldReplace?: boolean): void
```
Sets or appends the value of a specified HTTP response header. 

The **headerName** parameter defines the name of the header, example: "Content-Type."<br>
The **headerValue** parameter sets the value of the header, example: "text/html."<br>
The **shouldReplace** parameter determines whether to replace the value of a preexisting header or to append to it. 

**Example:**

```javascript
register((response, request) => 
{
    // This will replace the 'Server' header with the value 'custom server value'
    response.setHeader('Server', 'custom server value');

    // This will append 'custom server value 2' to the 'Server' header.
    response.setHeader('Server', 'custom server value 2', false);
    
    return CONTINUE;
});
```

#

### **GetHeader**

```ts
getHeader(headerName: string): string | null
```

Returns the value of a specified HTTP header. 

If the header doesn't exist this function will return *null* so make sure to check for it.

**Example:**

```javascript
register((response, request) => 
{
    // Gets the value of the server header.
    const serverHeaderValue = response.getHeader('Server');
    
    // Check if our header exists, in this case, it will.
    if (serverHeaderValue)
        // Prints out "Microsoft-IIS/10.0" (depending on your server version)
        print(serverHeaderValue);
   
    return CONTINUE;
});
```

#

### **DeleteHeader**

```ts
deleteHeader(headerName: String): void
```

Deletes an HTTP header from the request.

**Example:**

```javascript
register((response, request) => 
{
    response.deleteHeader('Server');

    return CONTINUE;
});
```

#

### **Clear**

```ts
clear(): void
```

Clears the response body. 

**Example:**
```javascript
register((response, request) => 
{
    response.clear();

    return CONTINUE;
});
```

#

### **ClearHeaders**

```ts
clearHeaders(): void
```

Clears the response headers and sets headers to default values.

**Example:**
```javascript
register((response, request) => 
{
    response.clearHeaders();

    return CONTINUE;
});
```
#

### **CloseConnection**

```ts
closeConnection(): void
```

Closes the connection and sends a reset packet to the client.

**Example:**
```javascript
register((response, request) => 
{
    response.closeConnection();

    return CONTINUE;
});
```
#

### **SetNeedDisconnect**

```ts
setNeedDisconnect(): void
```

Resets the socket after the response is complete.

**Example:**
```javascript
register((response, request) => 
{
    response.setNeedDisconnect();

    return CONTINUE;
});
```
#

### **GetKernelCacheEnabled**

```ts
getKernelCacheEnabled(): boolean
```

Determines whether the kernel cache is enabled for the current response.

**Example:**
```javascript
register((response, request) => 
{
    response.getKernelCacheEnabled();

    return CONTINUE;
});
```
#

### **ResetConnection**

```ts
resetConnection(): void
```

Resets the socket connection immediately.

**Example:**
```javascript
register((response, request) => 
{
    response.resetConnection();

    return CONTINUE;
});
```
#

### **GetStatus**

```ts
getStatus(): number
```

Retrieves the HTTP status for the response.

**Example:**
```javascript
register((response, request) => 
{
    const status = response.getStatus();
    
    return CONTINUE;
});
```

#

### **SetStatus**

```ts
setStatus(statusCode: Number, statusMessage: string): void
```

Sets the HTTP status for the response.

**Example:**
```javascript
register((response, request) => 
{
    response.setStatus(301, "Moved Permanently");
    
    return FINISH;
});
```

#

### **Redirect**

```ts
redirect(url: string, resetStatusCode: boolean, includeParameters: boolean): boolean
```

Redirects the client to a specified URL.

**Example:**
```javascript
register((response, request) => 
{
    response.redirect("/location", true /* resetStatusCode */, true /* includeParameters */);
    
    return CONTINUE;
});
```

#

### **SetErrorDescription**

```ts
setErrorDescription(decription: string, shouldHtmlEncode: boolean): boolean
```

Specifies the custom error description.

**Example:**
```javascript
register((response, request) => 
{
    response.setErrorDescription("error <b>description</b>", true /* shouldHtmlEncode */);
    
    return CONTINUE;
});
```
#

### **DisableKernelCache**

```ts
disableKernelCache(reason: number): void
```

Disables the kernel cache for this response.

**Example:**
```javascript
register((response, request) => 
{
    const HANDLER_HTTPSYS_UNFRIENDLY = 9;
    
    response.setErrorDescription(HANDLER_HTTPSYS_UNFRIENDLY);
    
    return CONTINUE;
});
```
