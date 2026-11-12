# Wrapper script to execute the pytest suite using the correct Windows Store Python interpreter.
# This prevents path shadowing conflicts with MSYS2/MinGW python.

$UserDir = $env:USERPROFILE
$python_exe = "$UserDir\AppData\Local\Microsoft\WindowsApps\python.exe"
if (-not (Test-Path $python_exe)) {
    $python_exe = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $python_exe) {
        Write-Error "Python executable not found in PATH or standard location."
        exit 1
    }
}

Write-Host "Running pytest suite via $python_exe..." -ForegroundColor Cyan
& $python_exe "$PSScriptRoot\run_tests.py" $args
