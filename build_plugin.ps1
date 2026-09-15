<#
.SYNOPSIS
    Automates cleaning, building, packaging, and zipping of the AgentFramework Unreal Engine plugin.
.DESCRIPTION
    This script searches for an installation of Unreal Engine, cleans the plugin's temporary build
    directories, runs the Unreal Automation Tool (UAT) to package the plugin, and compresses the
    output into a ZIP archive suitable for distribution on the Fab marketplace.
.PARAMETER EnginePath
    Explicit path to the Unreal Engine installation (e.g., "C:\Program Files\Epic Games\UE_5.4").
.PARAMETER UEVersion
    Target Unreal Engine version to search for (e.g., "5.4"). If omitted, the script searches for the latest installed UE 5.x version.
.PARAMETER OutputPath
    Directory where the packaged plugin and ZIP will be created. Defaults to "Packaged" in the repository root.
.PARAMETER NoZip
    If specified, skips compressing the packaged plugin into a ZIP file.
.PARAMETER TargetProjects
    Names of target game projects (under "$env:USERPROFILE\Documents\Unreal Projects") to deploy the packaged
    plugin into. Each project is only deployed to if "<project>\Plugins\AgentFramework" already exists.
    Defaults to "AgentFrameworkTest" (the standard verification project) and "tau-game".
    Pass an empty array to skip deployment entirely.
.PARAMETER ProjectsRoot
    Directory containing the target game projects. Defaults to "$env:USERPROFILE\Documents\Unreal Projects".
.EXAMPLE
    .\build_plugin.ps1 -UEVersion "5.4"
.EXAMPLE
    .\build_plugin.ps1 -EnginePath "C:\Program Files\Epic Games\UE_5.4" -OutputPath "C:\Builds\AgentFramework"
.EXAMPLE
    .\build_plugin.ps1 -NoZip -TargetProjects "AgentFrameworkTest"
#>

param (
    [string]$EnginePath = "",
    [string]$UEVersion = "",
    [string]$OutputPath = "Packaged",
    [switch]$NoZip,
    [string[]]$TargetProjects = @("AgentFrameworkTest", "tau-game"),
    [string]$ProjectsRoot = (Join-Path $env:USERPROFILE "Documents\Unreal Projects")
)

$ErrorActionPreference = "Stop"
$env:uebp_UATMutexNoWait = "1"
$env:UBT_bAllowUBA = "0"
[System.Environment]::SetEnvironmentVariable("uebp_UATMutexNoWait", "1", "Process")
[System.Environment]::SetEnvironmentVariable("UBT_bAllowUBA", "0", "Process")

# Get absolute path for repository root
$RepoRoot = Resolve-Path $PSScriptRoot
$PluginDir = if (Test-Path (Join-Path $RepoRoot "AgentFramework.uplugin")) { $RepoRoot } else { Join-Path $RepoRoot "AgentFramework" }
$UpluginPath = Join-Path $PluginDir "AgentFramework.uplugin"

if (-not (Test-Path $UpluginPath)) {
    Write-Error "Could not find AgentFramework.uplugin at '$UpluginPath'. Please run this script from the repository root."
}

# 1. Locate Unreal Engine installation
$ResolvedEnginePath = ""
if (-not [string]::IsNullOrEmpty($EnginePath)) {
    $ResolvedEnginePath = Resolve-Path $EnginePath
} else {
    Write-Host "Searching for Unreal Engine installation..." -ForegroundColor Cyan
    
    # List of versions to search for (newest first)
    $SearchVersions = @()
    if (-not [string]::IsNullOrEmpty($UEVersion)) {
        $SearchVersions += $UEVersion
    } else {
        # Try dynamic lookup first
        $ParentPaths = @("C:\Program Files\Epic Games", "C:\Program Files (x86)\Epic Games", "D:\Epic Games")
        foreach ($ParentPath in $ParentPaths) {
            if (Test-Path $ParentPath) {
                Get-ChildItem -Path $ParentPath -Directory -Filter "UE_5.*" | ForEach-Object {
                    if ($_.Name -match "UE_(5\.\d+)") {
                        $SearchVersions += $Matches[1]
                    }
                }
            }
        }
        # Add fallback hardcoded list to be safe, including newer versions
        $SearchVersions += @("5.9", "5.8", "5.7", "5.6", "5.5", "5.4", "5.3", "5.2", "5.1", "5.0")
        $SearchVersions = $SearchVersions | Select-Object -Unique | Sort-Object -Descending
    }

    foreach ($Ver in $SearchVersions) {
        # Standard Epic Games launcher paths
        $PathsToTry = @(
            "C:\Program Files\Epic Games\UE_$Ver",
            "C:\Program Files (x86)\Epic Games\UE_$Ver",
            "D:\Epic Games\UE_$Ver"
        )
        
        # Try registry lookup
        $RegPath = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Ver"
        if (Test-Path $RegPath) {
            $InstalledDir = Get-ItemProperty -Path $RegPath -Name "InstalledDirectory" -ErrorAction SilentlyContinue
            if ($InstalledDir -and $InstalledDir.InstalledDirectory) {
                $PathsToTry += $InstalledDir.InstalledDirectory
            }
        }

        foreach ($Path in $PathsToTry) {
            if (Test-Path $Path) {
                $ResolvedEnginePath = Resolve-Path $Path
                Write-Host "Found Unreal Engine $Ver at: $ResolvedEnginePath" -ForegroundColor Green
                break
            }
        }
        if (-not [string]::IsNullOrEmpty($ResolvedEnginePath)) { break }
    }
}

if ([string]::IsNullOrEmpty($ResolvedEnginePath)) {
    Write-Error "Could not locate Unreal Engine installation. Please specify a path using -EnginePath."
}

$RunUAT = Join-Path $ResolvedEnginePath "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $RunUAT)) {
    Write-Error "Could not find RunUAT.bat at expected path: '$RunUAT'"
}

# 1.5. Run C++ Tool Schema Audit
Write-Host "`nRunning C++ Tool Schema Audit via export_action_schemas.py..." -ForegroundColor Cyan
$ExportSchemaScript = Join-Path $RepoRoot "UnrealEngine\src\scripts\export_action_schemas.py"
if (Test-Path $ExportSchemaScript) {
    python $ExportSchemaScript
    if ($LASTEXITCODE -ne 0) {
        Write-Error "C++ Tool Schema Audit failed! Missing JSON schemas detected."
    }
}

# 2. Clean previous build folders from the plugin
Write-Host "`nCleaning intermediate and build folders in plugin directory..." -ForegroundColor Cyan
$FoldersToClean = @("Binaries", "Intermediate", "Saved")
foreach ($Folder in $FoldersToClean) {
    $TargetFolder = Join-Path $PluginDir $Folder
    if (Test-Path $TargetFolder) {
        Write-Host "Removing folder: $TargetFolder" -ForegroundColor Yellow
        Remove-Item -Path $TargetFolder -Recurse -Force
    }
}

# 3. Prepare output directory
# Ensure absolute output path
if (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $AbsoluteOutputPath = Join-Path $RepoRoot $OutputPath
} else {
    $AbsoluteOutputPath = $OutputPath
}

$PackagedPluginDir = Join-Path $AbsoluteOutputPath ("AFBuild_" + (Get-Date -Format "HHmmss") + "\AgentFramework")
if (Test-Path $AbsoluteOutputPath) {
    Write-Host "`nCleaning existing output directory '$AbsoluteOutputPath'..." -ForegroundColor Cyan
    # A build that has just finished can still be releasing handles under this directory. Failing
    # silently here leaves stale files behind and resurfaces much later as an opaque
    # "Failed to delete directory ...\HostProject" error from UAT, so retry briefly and then stop.
    $MaxCleanAttempts = 5
    for ($CleanAttempt = 1; $CleanAttempt -le $MaxCleanAttempts; $CleanAttempt++) {
        try {
            Remove-Item -Path $AbsoluteOutputPath -Recurse -Force -ErrorAction Stop
            break
        } catch {
            if ($CleanAttempt -eq $MaxCleanAttempts) {
                $Remaining = @(Get-ChildItem -Path $AbsoluteOutputPath -Recurse -Force -File -ErrorAction SilentlyContinue |
                    Select-Object -First 5 -ExpandProperty FullName)
                foreach ($Item in $Remaining) {
                    Write-Host "  Still present: $Item" -ForegroundColor Red
                }
                Write-Error "Failed to clean output directory '$AbsoluteOutputPath' after $MaxCleanAttempts attempts: $($_.Exception.Message) Close whatever is holding files under it (an Editor, a stray UnrealBuildTool, or an open file browser) and re-run."
            }
            Write-Host "Clean attempt $CleanAttempt/$MaxCleanAttempts failed; retrying in 2s..." -ForegroundColor Yellow
            Start-Sleep -Seconds 2
        }
    }
}
New-Item -ItemType Directory -Path $AbsoluteOutputPath -Force | Out-Null

# 4. Invoke UAT to build and package the plugin
$UbtLogPath = Join-Path $env:LOCALAPPDATA "UnrealBuildTool\Log.txt"
if (Test-Path $UbtLogPath) {
    Remove-Item -Path $UbtLogPath -Force -ErrorAction SilentlyContinue
}

& $RunUAT BuildPlugin -plugin="$UpluginPath" -package="$PackagedPluginDir" -Rocket -NoMutex
if ($LASTEXITCODE -ne 0) {
    Write-Error "BuildPlugin failed with exit code $LASTEXITCODE"
}

Write-Host "Build and packaging completed successfully!" -ForegroundColor Green

# 5. Compress the packaged output into a ZIP archive
if (-not $NoZip) {
    $ZipPath = Join-Path $AbsoluteOutputPath "AgentFramework.zip"
    Write-Host "`nCompressing packaged plugin into ZIP archive..." -ForegroundColor Cyan
    Write-Host "Zip destination: $ZipPath" -ForegroundColor DarkGray
    
    if (Test-Path $ZipPath) {
        Remove-Item -Path $ZipPath -Force
    }

    # Compress-Archive is built-in to PowerShell
    Compress-Archive -Path $PackagedPluginDir -DestinationPath $ZipPath -Force
    Write-Host "ZIP archive created successfully at '$ZipPath'!" -ForegroundColor Green
}

Write-Host "Copying compiled plugin binaries back to source and game project..." -ForegroundColor Cyan

# Copy to source directory
$SourceBinaries = Join-Path $PluginDir "Binaries"
$SourceIntermediate = Join-Path $PluginDir "Intermediate"
if (Test-Path $SourceBinaries) { Remove-Item -Path $SourceBinaries -Recurse -Force }
if (Test-Path $SourceIntermediate) { Remove-Item -Path $SourceIntermediate -Recurse -Force }
Copy-Item -Path (Join-Path $PackagedPluginDir "Binaries") -Destination $PluginDir -Recurse -Force
Copy-Item -Path (Join-Path $PackagedPluginDir "Intermediate") -Destination $PluginDir -Recurse -Force

# Copy to game projects
# The Editor keeps an exclusive lock on UnrealEditor-AgentFramework*.dll while a project is open.
# Wiping the plugin directory in that state leaves a half-deleted plugin behind, so every target is
# checked for a lock first and skipped outright rather than partially overwritten.

function Test-FileLocked {
    param([string]$Path)
    try {
        $Stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $Stream.Close()
        $Stream.Dispose()
        return $false
    } catch [System.IO.FileNotFoundException] {
        # Vanished between enumeration and probe - nothing to be locked.
        return $false
    } catch [System.IO.DirectoryNotFoundException] {
        return $false
    } catch [System.IO.IOException] {
        # Sharing violation: another process (the Editor) holds this binary.
        return $true
    } catch {
        # An ACL problem or similar - not a lock we can do anything about here.
        return $false
    }
}

function Get-LockedPluginBinaries {
    param([string]$PluginPath)
    $Locked = @()
    $BinariesDir = Join-Path $PluginPath "Binaries"
    if (-not (Test-Path $BinariesDir)) { return $Locked }
    Get-ChildItem -Path $BinariesDir -Filter "UnrealEditor-AgentFramework*.dll" -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        if (Test-FileLocked -Path $_.FullName) { $Locked += $_.FullName }
    }
    return $Locked
}

function Get-EditorProcessesForProject {
    param([string]$ProjectPath)
    # The Editor is launched with forward slashes in its command line
    # ("...UnrealEditor.exe" "C:/Users/.../AgentFrameworkTest/AgentFrameworkTest.uproject"),
    # so both sides are normalised to backslashes before comparing.
    $NormalisedProject = $ProjectPath.Replace('/', '\').TrimEnd('\')
    try {
        return @(Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%.exe'" -ErrorAction Stop |
            Where-Object {
                $_.CommandLine -and
                $_.CommandLine.Replace('/', '\').IndexOf($NormalisedProject, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
            })
    } catch {
        # CIM unavailable (restricted session): fall back to the file-lock probe alone.
        return @()
    }
}

$DeployFailures = @()
foreach ($ProjectName in $TargetProjects) {
    if ([string]::IsNullOrWhiteSpace($ProjectName)) { continue }

    $ProjectDir = Join-Path $ProjectsRoot $ProjectName
    $GamePluginDir = Join-Path $ProjectDir "Plugins\AgentFramework"

    if (-not (Test-Path $GamePluginDir)) {
        Write-Host "Skipping '$ProjectName': no plugin directory at '$GamePluginDir'." -ForegroundColor DarkGray
        continue
    }

    $EditorProcesses = @(Get-EditorProcessesForProject -ProjectPath $ProjectDir)
    $LockedBinaries = @(Get-LockedPluginBinaries -PluginPath $GamePluginDir)
    if ($EditorProcesses.Count -gt 0 -or $LockedBinaries.Count -gt 0) {
        Write-Warning "Skipping deployment to '$ProjectName': the Unreal Editor is running and holding the plugin binaries."
        foreach ($EditorProcess in $EditorProcesses) {
            Write-Warning "  $($EditorProcess.Name) (PID $($EditorProcess.ProcessId)) has this project open."
        }
        foreach ($LockedBinary in $LockedBinaries) {
            Write-Warning "  Locked: $LockedBinary"
        }
        Write-Warning "  Close the '$ProjectName' Editor and re-run this script. '$GamePluginDir' was left untouched."
        $DeployFailures += $ProjectName
        continue
    }

    Write-Host "Copying to game project: $GamePluginDir" -ForegroundColor Cyan
    try {
        $ExistingItems = @(Get-ChildItem -Path $GamePluginDir -Force -ErrorAction SilentlyContinue)
        if ($ExistingItems.Count -gt 0) {
            Remove-Item -Path $ExistingItems.FullName -Recurse -Force -ErrorAction Stop
        }
        Copy-Item -Path "$PackagedPluginDir\*" -Destination $GamePluginDir -Recurse -Force -ErrorAction Stop
        Write-Host "Deployed to '$ProjectName'." -ForegroundColor Green
    } catch {
        Write-Warning "Deployment to '$ProjectName' failed: $($_.Exception.Message)"
        Write-Warning "  This usually means the Unreal Editor opened the project mid-deployment and locked the binaries."
        Write-Warning "  Close the '$ProjectName' Editor and re-run this script; '$GamePluginDir' may be partially updated until then."
        $DeployFailures += $ProjectName
    }
}

if ($DeployFailures.Count -gt 0) {
    Write-Host "`nPackaged output located in '$AbsoluteOutputPath', but deployment was skipped for: $($DeployFailures -join ', ')." -ForegroundColor Yellow
    exit 1
}

Write-Host "`nWorkflow successfully completed. Packaged output located in '$AbsoluteOutputPath'." -ForegroundColor Green
