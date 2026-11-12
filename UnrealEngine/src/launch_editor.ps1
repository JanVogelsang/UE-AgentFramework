[CmdletBinding()]
param(
    [string]$ProjectPath,
    [string]$EditorExe,
    [string]$ExtraArgs
)

# 1. Resolve ProjectPath
if (-not $ProjectPath) {
    # Search upwards from current directory or script directory for *.uproject
    $SearchDir = $PSScriptRoot
    for ($i = 0; $i -lt 5; $i++) {
        $Found = Get-ChildItem -Path $SearchDir -Filter "*.uproject" -ErrorAction SilentlyContinue
        if ($Found) {
            $ProjectPath = $Found[0].FullName
            break
        }
        $SearchDir = Split-Path -Parent $SearchDir
    }
}

if ($ProjectPath) {
    # Trim trailing slashes/quotes
    $ProjectPath = $ProjectPath.Trim('"').Trim("'").TrimEnd('\').TrimEnd('/')
    
    # If path is a directory, resolve .uproject inside it
    if (Test-Path -Path $ProjectPath -PathType Container) {
        $Found = Get-ChildItem -Path $ProjectPath -Filter "*.uproject" -ErrorAction SilentlyContinue
        if ($Found) {
            $ProjectPath = $Found[0].FullName
        } else {
            Write-Error "No .uproject file found inside directory: $ProjectPath"
            exit 1
        }
    }
} else {
    Write-Error "Could not locate a .uproject file. Please specify -ProjectPath."
    exit 1
}

# Normalize to forward slashes to prevent Windows double-quote backslash escaping issues (\" -> quote escape)
$CleanProjectPath = $ProjectPath.Replace('\', '/')

# 2. Resolve EditorExe
if (-not $EditorExe) {
    # Try reading EngineAssociation from .uproject
    $EngineAssoc = "5.9"
    if (Test-Path $CleanProjectPath) {
        try {
            $Json = Get-Content -Raw -Path $CleanProjectPath | ConvertFrom-Json
            if ($Json.EngineAssociation) { $EngineAssoc = $Json.EngineAssociation }
        } catch {}
    }
    
    $EngineRoot = $null
    if ($EngineAssoc -match "^\d+\.\d+$") {
        $EngineRoot = (Get-ItemProperty -Path "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$EngineAssoc" -Name InstalledDirectory -ErrorAction SilentlyContinue).InstalledDirectory
    }
    if (-not $EngineRoot) {
        $BuildsKey = "HKCU:\Software\Epic Games\Unreal Engine\Builds"
        if (Test-Path $BuildsKey) {
            $EngineRoot = (Get-ItemProperty -Path $BuildsKey -Name $EngineAssoc -ErrorAction SilentlyContinue).$EngineAssoc
        }
    }
    if (-not $EngineRoot) {
        foreach ($p in @("D:\UE_$EngineAssoc", "C:\Program Files\Epic Games\UE_$EngineAssoc", "C:\Program Files (x86)\Epic Games\UE_$EngineAssoc", "D:\Epic Games\UE_$EngineAssoc")) {
            if (Test-Path $p) { $EngineRoot = $p; break }
        }
    }
    if ($EngineRoot) {
        $EditorExe = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    } else {
        $EditorExe = "C:/Program Files/Epic Games/UE_5.9/Engine/Binaries/Win64/UnrealEditor.exe"
    }
}

$CleanEditorExe = $EditorExe.Replace('\', '/')

if (-not (Test-Path $CleanEditorExe)) {
    Write-Error "UnrealEditor.exe not found at: $CleanEditorExe"
    exit 1
}

# 3. Construct CommandLine string
$CommandLine = "`"$CleanEditorExe`" `"$CleanProjectPath`""
if ($ExtraArgs) {
    $CommandLine += " $ExtraArgs"
}

Write-Host "Launching Unreal Editor:" -ForegroundColor Green
Write-Host "  CommandLine: $CommandLine" -ForegroundColor Cyan

# Launch process via WMI Win32_Process to decouple from child shell
$Result = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $CommandLine }
if ($Result.ReturnValue -eq 0) {
    Write-Host "Unreal Editor launched successfully (Process ID: $($Result.ProcessId))." -ForegroundColor Green
} else {
    Write-Error "Failed to launch Unreal Editor. Win32_Process ReturnValue: $($Result.ReturnValue)"
}
