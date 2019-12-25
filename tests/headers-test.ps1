.".\tests-helpers.ps1"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.setHeader('x-test-header', 'header value', false);

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Headers["x-test-header"] `
-expected_value "header value" `
-name "setHeader"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.setHeader('Server', 'new server', true);

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Headers["Server"] `
-expected_value "new server" `
-name "setHeader replace"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.setHeader('Server', 'new server', false);

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Headers["Server"].Contains("new server") `
-expected_value 1 `
-name "setHeader append"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.setHeader('x-test', '', false);

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Headers["x-test"] `
-expected_value '' `
-name "setHeader empty"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.deleteHeader('Server');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value ((Invoke-WebRequest -Uri "$url").Headers.ContainsKey("Server")) `
-expected_value 0 `
-name "deleteHeader"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.setHeader(
        'response-header', 
        request.getHeader('test-header')
    );

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url" -Headers @{"test-header" = "test-value"}).Headers["response-header"] `
-expected_value "test-value" `
-name "getHeader"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    if (request.getHeader("test-header")) {
        response.redirect("test-value", true, true);
        return RQ_NOTIFICATION_CONTINUE;
    }

    return RQ_NOTIFICATION_CONTINUE;
});
"@

$redirect_header = try { Invoke-WebRequest -Uri "$url" -Headers @{"test-header" = "test-value"} } 
catch { $_.Exception.Response.Headers["location"] }

TEST_EQUAL `
-value $redirect_header  `
-expected_value "test-value" `
-name "redirect"










