@echo OFF
echo Running all tests.
powershell.exe -ExecutionPolicy Bypass -File ".\headers-test.ps1"
powershell.exe -ExecutionPolicy Bypass -File ".\write-test.ps1"
powershell.exe -ExecutionPolicy Bypass -File ".\path-test.ps1"
pause