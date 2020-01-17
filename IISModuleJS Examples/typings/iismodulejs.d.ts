interface IISRequest {
    /**
     * Returns the HTTP request body as a string.
     * @param rewrite Determines whether to rewrite all the data back into the pipeline; 
     * set this to **true** if you expect to let the request continue to PHP or any other 
     * module which will use the request data.
     */
    read(rewrite?: boolean): void

    /**
     * Sets or appends the value of a specified HTTP request header.
     * @param headerName Defines the name of the header, example: "Content-Type." 
     * @param headerValue Sets the value of the header, example: "text/html."
     * @param shouldReplace Determines whether to replace the value of a preexisting header or to append to it.
     */
    setHeader(headerName: string, headerValue: string, shouldReplace?: boolean): void

    /**
     * Returns the value of a specified HTTP header. 
     * If the header doesn't exist this function will return null so make sure to check for it.
     * @param headerName The header name, example: "Content-Type." 
     */
    getHeader(headerName: string): string | null

    /**
     * Deletes an HTTP header from the request.
     * @param headerName The header name, example: "Content-Type." 
     */
    deleteHeader(headerName: string): void

    /**
     * Returns the host section of the URL for the request.
     */
    getHost(): string

    /**
     * Returns the query string for the request.
     */
    getQueryString(): string

    /**
     * Returns the full url for the request.
     */
    getFullUrl(): string

    /**
     * Returns the absolute path for the request.
     */
    getAbsPath(): string
    
    /**
     * Returns the HTTP method for the current request. Example: GET, POST, etc.
     */
    getMethod(): string

    /**
     * Returns the address of the local interface for the current request. 
     * This will return either a IPv4 or IPv6 address.
     */
    getMethod(): string

    /**
     * Returns the address of the remote interface for the current request. 
     * This will return either an IPv4 or IPv6 address.
     * 
     * This method can be used to get the connecting client's IP address.
     */
    getRemoteAddress(): string
}

interface IISResponse {
    /**
     * Writes the ``body`` as the response for the request.
     * @param body Gets written to the response.
     * @param mimeType Sets the Content-Type header with the given value.
     * @param contentEncoding Sets the Content-Encoding header so you can 
     * provide compressed data through a response.
     */
    write(body: string | Uint8Array, mimeType: string, contentEncoding?: string): void

    /**
     * Sets or appends the value of a specified HTTP request header.
     * @param headerName Defines the name of the header, example: "Content-Type." 
     * @param headerValue Sets the value of the header, example: "text/html."
     * @param shouldReplace Determines whether to replace the value of a preexisting header or to append to it.
     */
    setHeader(headerName: string, headerValue: string, shouldReplace?: boolean): void

    /**
     * Returns the value of a specified HTTP header. 
     * If the header doesn't exist this function will return null so make sure to check for it.
     * @param headerName The header name, example: "Content-Type." 
     */
    getHeader(headerName: string): string | null

    /**
     * Deletes an HTTP header from the request.
     * @param headerName The header name, example: "Content-Type." 
     */
    deleteHeader(headerName: string): void

    /**
     * Clears the response body.
     */
    clear(): void

    /**
     * Clears the response headers and sets headers to default values.
     */
    clearHeaders(): void

    /**
     * Closes the connection and sends a reset packet to the client.
     */
    closeConnection(): void

    /**
     * Resets the socket connection immediately.
     */
    resetConnection(): void

    /**
     * Resets the socket after the response is complete.
     */
    setNeedDisconnect(): void

    /**
     * Determines whether the kernel cache is enabled 
     * for the current response.
     */
    getKernelCacheEnabled(): boolean

    /**
     * Retrieves the HTTP status for the response.
     */
    getStatus(): number

    /**
     * Redirects the client to a specified URL.
     * @param url 
     * @param resetStatusCode 
     * @param includeParameters 
     */
    redirect(url: string, resetStatusCode: boolean, includeParameters: boolean): boolean

    /**
     * Specifies the custom error description.
     * @param decription 
     * @param shouldHtmlEncode 
     */
    setErrorDescription(decription: string, shouldHtmlEncode: boolean): boolean

    /**
     * Disables the kernel cache for this response.
     * @param reason 
     */
    disableKernelCache(reason: number): void
}

interface HTTP {
    /**
     * This function only supports **POST** and **GET** methods and should **not** be mistaken
     * for the [fetch](https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API) found in the standard web api.
     * 
     * @param hostname Specifies the domain name of the server.
     * @param path Specifies the path part of the HTTP request.
     * @param isSSL Specifies whether the endpoint is a secure endpoint using TLS.
     * @param method Specifies the method part of the HTTP request, only, **GET** and **POST**.
     * @param params Should be an Object (not an Array) which 
     * will represent the collection of key-value POST parameters 
     * (imagine an HTML form) for the HTTP POST request.
     */
    fetch(
        hostname: string, 
        path: string, 
        isSSL: boolean, 
        method?: string, 
        params?: Object
    ): 
    Promise<{
        readonly body: string;
        readonly status: number;
    }>
}

/**
 * The interprocess communication interface provides a key-value 
 * store where you can share JavaScript data across different processes/workers.
 */
interface IPC {
    /**
     * Sets a **key** with a given **value**.
     * @param key The key to use.
     * @param value  The value to set the key with.
     */
    set(key: string, value: any): void

    /**
     * Returns a value with the corresponding key. 
     * 
     * This function will return **null** if the key does not exist, make sure to check for it.
     * @param key The key containing the value.
     */
    get(key: string): any | null
}

/**
 * Callback for adding two numbers.
 *
 * @callback addStuffCallback
 * @param {int} sum - An integer.
 */

/**
 * Registers a given function as a callback which will be called for every request.
 * 
 * The callback function can also be asynchronous but keep in mind that this will yield far worse performance 
 * than using an ordinary function.
 * @param {addStuffCallback} callback A callback function which will be provided a ``Response`` object, and a ``Request`` object respectively.
 */
declare function register(
    callback: (response: IISResponse, request: IISRequest) => number | Promise<number>
): void;

/**
 * The interprocess communication interface provides a key-value 
 * store where you can share JavaScript data across different processes/workers.
 */
declare var ipc: IPC;

/**
 * The HTTP interface allowing to communicate with remote endpoints.
 */
declare var http: HTTP;

/**
 * Loads a script using ``fileName``.
 * @param fileName The file name of the JavaScript file, the name should include the extension.
 */
declare function load(...fileName: string[]): void;

/**
 * Prints ``msg`` using OutputDebugstring. You can observe the print out using a debugger or DebugView.
 * @param msg The message to print. Each message component will be seperated by a space character.
 */
declare function print(...msg: string[]): void;

/**
 * Indicates that IIS should continue processing additional request-level notifications.
 */
declare const RQ_NOTIFICATION_CONTINUE = 0;

/**
 * ## DO NOT USE, FOR INTERNAL USE ONLY
 * Indicates that an asynchronous notification is pending and returns request-level processing to IIS. 
 */
declare const RQ_NOTIFICATION_PENDING = 1; 

/**
 * Indicates that IIS has finished processing request-level notifications and
 * should not process any additional request-level notifications.
 */
declare const RQ_NOTIFICATION_FINISH_REQUEST = 2;

/**
 * Disconnects the network connection immediately after the response is sent.
 */
declare const HTTP_SEND_RESPONSE_FLAG_DISCONNECT = 0x00000001;

/**
 * Sends additional data in the response.
 */
declare const HTTP_SEND_RESPONSE_FLAG_MORE_DATA = 0x00000002;

/**
 * Buffers the response before it is sent.
 */
declare const HTTP_SEND_RESPONSE_FLAG_BUFFER_DATA = 0x00000004;

/**
 * Enables the Nagle algorithm to optimize TCP response packets.
 */
declare const HTTP_SEND_RESPONSE_FLAG_ENABLE_NAGLING = 0x00000008;