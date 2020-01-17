/// <reference path="../typings/iismodulejs.d.ts" />

/**
 * The callback function which will be called for each request.
 * @param {IISRequest} response 
 * @param {IISResponse} request 
 */
function callback(response, request)
{
    return RQ_NOTIFICATION_CONTINUE;
}    
  
// Registers the callback function.
register(callback);