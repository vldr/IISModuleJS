$url = "http://127.0.0.1"

function TEST_LOAD_SCRIPT ($script) {
    Add-Type -AssemblyName System.Web

    $formatted_script = ($script -replace '(^\s+|\s+$)','' -replace '\s+',' ')

    #Write-Host $formatted_script
    $response = Invoke-WebRequest -Uri "$url" -Headers @{"x-execute-code" = $formatted_script}

    if ($response.Headers["x-did-execute"] -eq "0")
    {
        Write-Host $response.Headers["x-did-execute"]
        Write-Host "[ script failed to compile ]" -ForegroundColor Red
    }
}

function TEST_EQUAL($name, $value, $expected_value) {
    if ($value -eq $expected_value) 
    { 
        Write-Host  "[ test - '$name' - passed ]" -ForegroundColor Green
    }
    else 
    { 
        Write-Host "[ test - '$name' - failed ]" -ForegroundColor Red
        Write-Host "[ value: '$value' - expected: '$expected_value' ]" -ForegroundColor Red
    }
}