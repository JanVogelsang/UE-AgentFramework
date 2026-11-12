# Consolidated Test Runner for UE-AgentFramework (Unreal Engine 5.9)
# This script runs both the C++ headless Unreal Editor automation tests
# and the Python integration/unit tests sequentially.

$ErrorActionPreference = "Stop"

# Resolve project path dynamically
$project_path = "$env:USERPROFILE\Documents\Unreal Projects\AgentFrameworkTest\AgentFrameworkTest.uproject"

# Resolve Python dynamically
$python_exe = "$env:USERPROFILE\AppData\Local\Microsoft\WindowsApps\python.exe"
if (-not (Test-Path $python_exe)) {
    $python_exe = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $python_exe) {
        Write-Error "Python executable not found in PATH or standard location."
        exit 1
    }
}

# Resolve Unreal Engine dynamically based on the project's EngineAssociation
$engine_association = "5.9" # Default fallback
if (Test-Path $project_path) {
    try {
        $uproject_json = Get-Content -Raw -Path $project_path | ConvertFrom-Json
        if ($uproject_json.EngineAssociation) {
            $engine_association = $uproject_json.EngineAssociation
        }
    } catch {
        Write-Host "Warning: Failed to parse uproject JSON. Using default association 5.9." -ForegroundColor Yellow
    }
}

$installed_dir = $null

if ($engine_association -match "^\d+\.\d+$") {
    $RegistryPath = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$engine_association"
    $installed_dir = (Get-ItemProperty -Path $RegistryPath -Name InstalledDirectory -ErrorAction SilentlyContinue).InstalledDirectory
    
    if (-not $installed_dir) {
        $BuildsKey = "HKCU:\Software\Epic Games\Unreal Engine\Builds"
        if (Test-Path $BuildsKey) {
            $installed_dir = (Get-ItemProperty -Path $BuildsKey -Name $engine_association -ErrorAction SilentlyContinue).$engine_association
        }
    }
    
    if (-not $installed_dir) {
        $FallbackPaths = @(
            "D:\UE_$engine_association",
            "C:\Program Files\Epic Games\UE_$engine_association",
            "D:\Epic Games\UE_$engine_association"
        )
        foreach ($p in $FallbackPaths) {
            if (Test-Path $p) {
                $installed_dir = $p
                break
            }
        }
    }
} else {
    $BuildsKey = "HKCU:\Software\Epic Games\Unreal Engine\Builds"
    if (Test-Path $BuildsKey) {
        $installed_dir = (Get-ItemProperty -Path $BuildsKey -Name $engine_association -ErrorAction SilentlyContinue).$engine_association
    }
}

if (-not $installed_dir) {
    if (Test-Path "D:\UE_5.9") {
        $installed_dir = "D:\UE_5.9"
    } else {
        $installed_dir = "C:\Program Files\Epic Games\UE_5.9"
    }
}

$installed_dir = $installed_dir.TrimEnd('\')
$unreal_exe = Join-Path $installed_dir "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path $unreal_exe)) {
    $unreal_exe = Join-Path $installed_dir "Engine\Binaries\Win64\UnrealEditor.exe"
}

Write-Host "=================================================================" -ForegroundColor Magenta
Write-Host "       UE-AgentFramework (UE 5.9) Consolidated Test Runner       " -ForegroundColor Magenta
Write-Host "=================================================================" -ForegroundColor Magenta

# 1. Verify environment paths
if (-not (Test-Path $unreal_exe)) {
    Write-Error "Unreal Editor executable not found at: $unreal_exe"
    exit 1
}
if (-not (Test-Path $project_path)) {
    Write-Error "Target game project not found at: $project_path"
    exit 1
}
if (-not (Test-Path $python_exe)) {
    Write-Error "Python interpreter not found at: $python_exe"
    exit 1
}

# 2. Run C++ Headless Automation Tests
Write-Host "`n[1/2] Running C++ Headless Unreal Editor 5.9 Automation Tests..." -ForegroundColor Cyan
Write-Host "Command: UnrealEditor-Cmd.exe -NullRHI -NoSound -NoSourceControl -unattended -ExecCmds=`"Automation RunTests AgentFramework.Toolsets; Quit`"" -ForegroundColor DarkGray

$processInfo = New-Object System.Diagnostics.ProcessStartInfo
$processInfo.FileName = $unreal_exe
$processInfo.Arguments = "`"$project_path`" -NullRHI -NoSound -NoSourceControl -unattended -ExecCmds=`"Automation RunTests AgentFramework.Toolsets; Quit`" -log"
$processInfo.UseShellExecute = $false
$processInfo.RedirectStandardOutput = $false
$processInfo.RedirectStandardError = $false

$process = [System.Diagnostics.Process]::Start($processInfo)
$process.WaitForExit()
$cpp_exit_code = $process.ExitCode

if ($cpp_exit_code -ne 0) {
    Write-Host "`n[-] C++ Automation Tests FAILED with exit code: $cpp_exit_code" -ForegroundColor Red
    $project_dir = Split-Path -Parent $project_path
    Write-Host "Check the project logs at: $project_dir\Saved\Logs\AgentFrameworkTest.log" -ForegroundColor Yellow
    exit $cpp_exit_code
} else {
    Write-Host "`n[+] C++ Headless Automation Tests PASSED successfully!" -ForegroundColor Green
}

# 3. Run Python Unit & Integration Tests
Write-Host "`n[2/2] Running Python unit and integration tests..." -ForegroundColor Cyan
Write-Host "Command: python -m pytest" -ForegroundColor DarkGray

$python_process = Start-Process -FilePath $python_exe -ArgumentList "`"$PSScriptRoot\run_tests.py`"" -Wait -NoNewWindow -PassThru
$python_exit_code = $python_process.ExitCode

if ($python_exit_code -ne 0) {
    Write-Host "`n[-] Python Tests FAILED with exit code: $python_exit_code" -ForegroundColor Red
    exit $python_exit_code
} else {
    Write-Host "`n[+] Python Tests PASSED successfully!" -ForegroundColor Green
}

Write-Host "`n=================================================================" -ForegroundColor Magenta
Write-Host "   [SUCCESS] All C++ and Python tests passed headlessly!   " -ForegroundColor Green
Write-Host "=================================================================" -ForegroundColor Magenta
