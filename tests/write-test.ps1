.".\tests-helpers.ps1"

##############################################################################

$big_text = Get-Content -Path ".\big-string.txt"
$big_array = Get-Content -Path ".\big-array.txt"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('$big_text', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "$big_text" `
-name "write 65536 byte long string"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('$big_text$big_text', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "$big_text$big_text" `
-name "write 131072 byte long string"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "" `
-name "write 0 byte long string"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('hi', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "hi" `
-name "write 2 byte long string"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write(new Uint8Array([$big_array]), 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value $big_text `
-name "write 65536 byte long array"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write(new Uint8Array([$big_array, $big_array]), 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value $big_text$big_text `
-name "write 131072 byte long array"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write(new Uint8Array([]), 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "" `
-name "write 0 byte long array"

##############################################################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write(new Uint8Array([0x68, 0x69]), 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "hi" `
-name "write 2 byte long array"


