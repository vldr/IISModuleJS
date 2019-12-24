$url = "http://127.0.0.1"

function TEST_LOAD_SCRIPT ($script) {
    $public_folder = $Env:PUBLIC
    $iis_web_worker = Get-Process -Name "w3wp" -IncludeUserName
    $poolname = $iis_web_worker.UserName.Split("\")[1];
    
    Write-Host "[ loading script ]" -ForegroundColor White
    
    Set-Content -Path "$public_folder\$poolname.js" -Force -Value $script
    Start-Sleep -Seconds 1 
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