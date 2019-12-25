.".\tests-helpers.ps1"

####################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    if (request.getHeader("x-test-value") == "getAbsPath") response.write('' + request.getAbsPath(), 'text/html'); 
    if (request.getHeader("x-test-value") == "getMethod") response.write('' + request.getMethod(), 'text/html'); 
    if (request.getHeader("x-test-value") == "getFullUrl") response.write('' + request.getFullUrl(), 'text/html'); 
    if (request.getHeader("x-test-value") == "getQueryString") response.write('' + request.getQueryString(), 'text/html'); 
    if (request.getHeader("x-test-value") == "getHost") response.write('' + request.getHost(), 'text/html'); 
    if (request.getHeader("x-test-value") == "getLocalAddress") response.write('' + request.getLocalAddress(), 'text/html'); 
    if (request.getHeader("x-test-value") == "getRemoteAddress") response.write('' + request.getRemoteAddress(), 'text/html'); 

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

#######################################
# getAbsPath test
#######################################

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/this/is/a/absolute/path?this=is&a=query&string" -Headers @{"x-test-value"="getAbsPath"}).Content `
-expected_value "/this/is/a/absolute/path" `
-name "getAbsPath sample"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url" -Headers @{"x-test-value"="getAbsPath"}).Content `
-expected_value "/" `
-name "getAbsPath empty"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/?this=is&a=query&string" -Headers @{"x-test-value"="getAbsPath"}).Content `
-expected_value "/" `
-name "getAbsPath query string"

#######################################
# getMethod test
#######################################

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/getMethod" -Headers @{"x-test-value"="getMethod"} -Method Get).Content `
-expected_value "GET" `
-name "getMethod GET"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/getMethod" -Headers @{"x-test-value"="getMethod"} -Method Post).Content `
-expected_value "POST" `
-name "getMethod POST"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/getMethod" -Headers @{"x-test-value"="getMethod"} -Method Put).Content `
-expected_value "PUT" `
-name "getMethod PUT"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/getMethod" -Headers @{"x-test-value"="getMethod"} -Method Delete).Content `
-expected_value "DELETE" `
-name "getMethod DELETE"

#######################################
# getFullUrl test
#######################################

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/this/is/a/absolute/path?this=is&a=query&string" -Headers @{"x-test-value"="getFullUrl"} ).Content `
-expected_value "$url`:80/this/is/a/absolute/path?this=is&a=query&string" `
-name "getFullUrl"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url" -Headers @{"x-test-value"="getFullUrl"} ).Content `
-expected_value "$url`:80/" `
-name "getFullUrl empty"

#######################################
# getQueryString test
#######################################

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/?this=is&a=query&string" -Headers @{"x-test-value"="getQueryString"} ).Content `
-expected_value "?this=is&a=query&string" `
-name "getQueryString"

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/this/is/a/absolute/path?this=is&a=query&string" -Headers @{"x-test-value"="getQueryString"} ).Content `
-expected_value "?this=is&a=query&string" `
-name "getQueryString with abs path"

#######################################
# getHost test
#######################################

$ip_from_url = $url.Replace("http://", "")

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/this/is/a/absolute/path?this=is&a=query&string" -Headers @{"x-test-value"="getHost"} ).Content `
-expected_value "$ip_from_url`:80" `
-name "getHost"

#######################################
# getLocalAddress test
#######################################

$ip_from_url = $url.Replace("http://", "")

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/this/is/a/absolute/path?this=is&a=query&string" -Headers @{"x-test-value"="getLocalAddress"} ).Content `
-expected_value "$ip_from_url" `
-name "getLocalAddress"

#######################################
# getRemoteAddress test
#######################################

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url/this/is/a/absolute/path?this=is&a=query&string" -Headers @{"x-test-value"="getRemoteAddress"} ).Content `
-expected_value "$ip_from_url" `
-name "getRemoteAddress"
