load("URI.js");
load("CryptoJS.js");
load("SHA1.js");

var Twitter = {};

Twitter.consumerKey = "";
Twitter.consumerKeySecret = "";
Twitter.accessToken = "";
Twitter.accessTokenSecret = "";

Twitter.request = function(method, path, data, params = {})
{
    /////////////////////////////////////////

    const randomString = (length) => 
    {
        let result = '';
        let characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
        let charactersLength = characters.length;

        for (let i = 0; i < length; i++) 
        {
            result += characters.charAt(
                Math.floor(
                    Math.random() * charactersLength
                )
            );
        }

        return result; 
    };

    /////////////////////////////////////////

    const timestamp = Math.round(Date.now() / 1000);
    const nonce = randomString(32);

    /////////////////////////////////////////

    const queryString = URI.buildQuery(params);

    /////////////////////////////////////////

    params.oauth_consumer_key = Twitter.consumerKey;
    params.oauth_token = Twitter.accessToken;
    params.oauth_signature_method = "HMAC-SHA1";
    params.oauth_timestamp = timestamp;
    params.oauth_nonce = nonce;
    params.oauth_version = "1.0";

    /////////////////////////////////////////
   
    const signatureKey = `${encodeURIComponent(Twitter.consumerKeySecret)}&${encodeURIComponent(Twitter.accessTokenSecret)}`;
    
    /////////////////////////////////////////

    const keys = Object.keys(params);
    const ordered = {};
    
    keys.forEach(function (item, i) {
        keys[i] = item;
    })

    keys.sort().forEach(function(key) {
        ordered[key] = params[key];
    });

    /////////////////////////////////////////

    let baseString = `${method}&${encodeURIComponent(`https://api.twitter.com/1.1/${path}`)}&`;

    baseString += encodeURIComponent(
        URI.buildQuery(ordered, false, true)
    );

    /////////////////////////////////////////

    const signature = encodeURIComponent(
        CryptoJS.enc.Base64.stringify(
            CryptoJS.HmacSHA1(
                baseString, 
                signatureKey
            )
        )
    ); 
 
    /////////////////////////////////////////

    return http.fetch("api.twitter.com", `/1.1/${path}?${queryString}`, 
    {
        method: method,
        is_ssl: true,
        headers: {
            "Authorization": 
            `OAuth ` +
            `oauth_consumer_key="${encodeURIComponent(Twitter.consumerKey)}", ` +
            `oauth_token="${encodeURIComponent(Twitter.accessToken)}", ` +
            `oauth_signature_method="HMAC-SHA1", ` +
            `oauth_timestamp="${timestamp}", ` +
            `oauth_signature="${signature}", ` +
            `oauth_nonce="${encodeURIComponent(nonce)}", ` +
            `oauth_version="1.0"`
        },
        body: JSON.stringify(data)
    });
}

Twitter.handleCRC = function(response, request)
{
    const queryString = URI.parseQuery(
        request.getQueryString()
    );

    if (queryString.crc_token)
    {
        const res = {
            response_token: "sha256=" + CryptoJS.enc.Base64.stringify(
                CryptoJS.HmacSHA256(
                    queryString.crc_token, 
                    Twitter.consumerKeySecret
                )
            )
        }

        response.write(
            JSON.stringify(res)
        )

        return true;
    }
    
    return false;
}

Twitter.get = function(path, params)
{
    return Twitter.request("GET", path, {}, params);
}

Twitter.post = function(path, data, params = {})
{
    return Twitter.request("POST", path, data, params);
}

Twitter.delete = function(path, data, params = {})
{
    return Twitter.request("DELETE", path, data, params);
}
