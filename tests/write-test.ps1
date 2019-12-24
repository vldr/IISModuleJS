.".\tests-helpers.ps1"

#######################################
# 64kb write test 
#######################################

$rand = New-Object System.Random
1..65355 | foreach { $random_characters = $random_characters + [char]$rand.next(65,90) }

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('$random_characters', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "$random_characters" `
-name "write 65536 byte long string"

#######################################
# 131kb write test 
#######################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('$random_characters$random_characters', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "$random_characters$random_characters" `
-name "write 131072 byte long string"

#######################################
# 0 byte write test 
#######################################

TEST_LOAD_SCRIPT -script @"
register((response, request) => {
    response.write('$random_characters$random_characters', 'text/html');

    return RQ_NOTIFICATION_FINISH_REQUEST;
});
"@

TEST_EQUAL `
-value (Invoke-WebRequest -Uri "$url").Content `
-expected_value "$random_characters$random_characters" `
-no_output 1 `
-name "write 0 byte long string"
