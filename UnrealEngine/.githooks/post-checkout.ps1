param(
    [string]$PreviousHead,
    [string]$NewHead,
    [string]$CheckoutType
)

# Output log file for debugging
$LogFile = Join-Path $PSScriptRoot "post-checkout.log"
function Log-Message($Message) {
    $Timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    "[$Timestamp] $Message" | Out-File -FilePath $LogFile -Append
    Write-Host $Message
}

Log-Message "Git post-checkout hook triggered."
Log-Message "Arguments: PreviousHead='$PreviousHead', NewHead='$NewHead', CheckoutType='$CheckoutType'"

# 1. Check if we are in a Git worktree
$Cwd = (Get-Location).Path
$DotGitPath = Join-Path $Cwd ".git"
if (-not (Test-Path $DotGitPath)) {
    Log-Message "Error: .git path not found in current directory ($Cwd). Exiting."
    exit 0
}

$DotGitItem = Get-Item $DotGitPath -Force
if ($DotGitItem.PSIsContainer) {
    Log-Message "This is the main repository (not a worktree). Skipping initialization."
    exit 0
}

# 2. Idempotency Check: Verify if .agents is already configured
$WorktreeRoot = $Cwd
$WorktreeAgentsDir = Join-Path $WorktreeRoot ".agents"
$WorktreePluginDir = Join-Path $WorktreeAgentsDir "plugins\UnrealEngine"

if (Test-Path $WorktreePluginDir) {
    Log-Message "AgentFramework plugin already initialized in this worktree. Skipping."
    exit 0
}

Log-Message "Initializing AgentFramework workspace in new worktree: $WorktreeRoot"

# 3. Locate Main Repository Root
try {
    $GitCommonDir = [System.IO.Path]::GetFullPath((git rev-parse --git-common-dir))
    $MainRepoRoot = Split-Path -Parent $GitCommonDir
    Log-Message "Resolved main repository root: $MainRepoRoot"
} catch {
    Log-Message "Error: Failed to resolve main repository path. Exiting."
    exit 1
}

$MainAgentsDir = Join-Path $MainRepoRoot ".agents"
$MainPluginDir = Join-Path $MainAgentsDir "plugins\UnrealEngine"

# 4. Construct Directories
if (-not (Test-Path $WorktreeAgentsDir)) {
    New-Item -ItemType Directory -Force -Path $WorktreeAgentsDir | Out-Null
}
$WorktreeExtServerDir = Join-Path $WorktreePluginDir "ExternalServer"
if (-not (Test-Path $WorktreeExtServerDir)) {
    New-Item -ItemType Directory -Force -Path $WorktreeExtServerDir | Out-Null
}

# 5. Copy Workspace rules (AGENTS.md)
$MainAgentsMd = Join-Path $MainAgentsDir "AGENTS.md"
$WorktreeAgentsMd = Join-Path $WorktreeAgentsDir "AGENTS.md"
if (Test-Path $MainAgentsMd) {
    Copy-Item -Path $MainAgentsMd -Destination $WorktreeAgentsMd -Force
    Log-Message "Copied AGENTS.md workspace rules."
}

# 6. Symlink Shared/Static Resources (Junctions for Windows compatibility without admin rights)
$MainVenv = Join-Path $MainPluginDir "ExternalServer\.venv"
$WorktreeVenv = Join-Path $WorktreeExtServerDir ".venv"
if (Test-Path $MainVenv) {
    New-Item -ItemType Junction -Path $WorktreeVenv -Value $MainVenv -Force | Out-Null
    Log-Message "Junction created for Python virtual environment."
} else {
    Log-Message "Warning: Main virtual environment not found at $MainVenv"
}

$VectorDbDirs = Get-ChildItem -Path (Join-Path $MainPluginDir "ExternalServer") -Directory -Filter "vector_db_*" -ErrorAction SilentlyContinue
foreach ($MainVectorDb in $VectorDbDirs) {
    $WorktreeVectorDb = Join-Path $WorktreeExtServerDir $MainVectorDb.Name
    New-Item -ItemType Junction -Path $WorktreeVectorDb -Value $MainVectorDb.FullName -Force | Out-Null
    Log-Message "Junction created for static documentation Vector DB ($($MainVectorDb.Name))."
}

# 7. Copy Stateful/Configuration files
$MainAstCache = Join-Path $MainPluginDir "ExternalServer\ast_cache.db"
$WorktreeAstCache = Join-Path $WorktreeExtServerDir "ast_cache.db"
if (Test-Path $MainAstCache) {
    Copy-Item -Path $MainAstCache -Destination $WorktreeAstCache -Force
    Log-Message "Copied C++ AST cache database."
} else {
    Log-Message "Warning: C++ AST cache database not found at $MainAstCache. It will be built locally."
}

$MainMcp = Join-Path $MainPluginDir "mcp_config.json"
$WorktreeMcp = Join-Path $WorktreePluginDir "mcp_config.json"
if (Test-Path $MainMcp) {
    Copy-Item -Path $MainMcp -Destination $WorktreeMcp -Force
    Log-Message "Copied MCP configuration."
}

# The precompiled bridge.exe was retired in 5591b0c (2026-06-30) in favour of the Python
# bridge, which is linked as a junction further below. Copying a stray leftover binary here
# would seed new worktrees with a months-old bridge, so nothing is copied for it.

# 8. Symlink Plugin Source Code (Junctions to the worktree's own branch source code or main repo copies)
# A. bridge/
$WorktreeTrackedBridge = Join-Path $WorktreeRoot "UnrealEngine\bridge"
$WorktreePluginBridge = Join-Path $WorktreePluginDir "bridge"
if (Test-Path $WorktreeTrackedBridge) {
    New-Item -ItemType Junction -Path $WorktreePluginBridge -Value $WorktreeTrackedBridge -Force | Out-Null
    Log-Message "Linked live bridge source code (development mode)."
} else {
    $MainBridgeDir = Join-Path $MainPluginDir "bridge"
    if (Test-Path $MainBridgeDir) {
        New-Item -ItemType Junction -Path $WorktreePluginBridge -Value $MainBridgeDir -Force | Out-Null
        Log-Message "Linked main repo bridge source code (plugin mode)."
    }
}

# B. ExternalServer/src
$WorktreeTrackedExtSrc = Join-Path $WorktreeRoot "UnrealEngine\ExternalServer\src"
$WorktreePluginExtSrc = Join-Path $WorktreeExtServerDir "src"
if (Test-Path $WorktreeTrackedExtSrc) {
    New-Item -ItemType Junction -Path $WorktreePluginExtSrc -Value $WorktreeTrackedExtSrc -Force | Out-Null
    Log-Message "Linked live ExternalServer/src folder (development mode)."
} else {
    $MainExtSrcDir = Join-Path $MainPluginDir "ExternalServer\src"
    if (Test-Path $MainExtSrcDir) {
        New-Item -ItemType Junction -Path $WorktreePluginExtSrc -Value $MainExtSrcDir -Force | Out-Null
        Log-Message "Linked main repo ExternalServer/src folder (plugin mode)."
    }
}

# C. ExternalServer/scripts
$WorktreeTrackedScripts = Join-Path $WorktreeRoot "UnrealEngine\ExternalServer\scripts"
$WorktreePluginScripts = Join-Path $WorktreeExtServerDir "scripts"
if (Test-Path $WorktreeTrackedScripts) {
    New-Item -ItemType Junction -Path $WorktreePluginScripts -Value $WorktreeTrackedScripts -Force | Out-Null
    Log-Message "Linked live ExternalServer/scripts folder (development mode)."
} else {
    $MainScriptsDir = Join-Path $MainPluginDir "ExternalServer\scripts"
    if (Test-Path $MainScriptsDir) {
        New-Item -ItemType Junction -Path $WorktreePluginScripts -Value $MainScriptsDir -Force | Out-Null
        Log-Message "Linked main repo ExternalServer/scripts folder (plugin mode)."
    }
}

# D. src (contains generate_env_skill.ps1)
$WorktreeTrackedSrc = Join-Path $WorktreeRoot "UnrealEngine\src"
$WorktreePluginSrc = Join-Path $WorktreePluginDir "src"
if (Test-Path $WorktreeTrackedSrc) {
    New-Item -ItemType Junction -Path $WorktreePluginSrc -Value $WorktreeTrackedSrc -Force | Out-Null
    Log-Message "Linked live src folder (development mode)."
} else {
    $MainSrcDir = Join-Path $MainPluginDir "src"
    if (Test-Path $MainSrcDir) {
        New-Item -ItemType Junction -Path $WorktreePluginSrc -Value $MainSrcDir -Force | Out-Null
        Log-Message "Linked main repo src folder (plugin mode)."
    }
}

# E. skills
$WorktreeTrackedSkills = Join-Path $WorktreeRoot "UnrealEngine\skills"
$WorktreePluginSkills = Join-Path $WorktreePluginDir "skills"
if (Test-Path $WorktreeTrackedSkills) {
    New-Item -ItemType Junction -Path $WorktreePluginSkills -Value $WorktreeTrackedSkills -Force | Out-Null
    Log-Message "Linked live skills directory (development mode)."
} else {
    if (-not (Test-Path $WorktreePluginSkills)) {
        New-Item -ItemType Directory -Force -Path $WorktreePluginSkills | Out-Null
    }
    $MainSkillsDir = Join-Path $MainPluginDir "skills"
    if (Test-Path $MainSkillsDir) {
        Get-ChildItem -Path $MainSkillsDir -Directory | ForEach-Object {
            $SkillName = $_.Name
            $WorktreeSkillPath = Join-Path $WorktreePluginSkills $SkillName
            New-Item -ItemType Junction -Path $WorktreeSkillPath -Value $_.FullName -Force | Out-Null
        }
        Log-Message "Linked main repo shared skills (plugin mode)."
    }
}

# 9. Hardlink Tracked Configs/JSON
$WorktreeTrackedReqs = Join-Path $WorktreeRoot "UnrealEngine\ExternalServer\requirements.txt"
$WorktreePluginReqs = Join-Path $WorktreeExtServerDir "requirements.txt"
if (Test-Path $WorktreeTrackedReqs) {
    New-Item -ItemType HardLink -Path $WorktreePluginReqs -Value $WorktreeTrackedReqs -Force | Out-Null
} else {
    $MainReqs = Join-Path $MainPluginDir "ExternalServer\requirements.txt"
    if (Test-Path $MainReqs) {
        New-Item -ItemType HardLink -Path $WorktreePluginReqs -Value $MainReqs -Force | Out-Null
    }
}

$WorktreeTrackedPluginJson = Join-Path $WorktreeRoot "UnrealEngine\plugin.json"
$WorktreePluginJson = Join-Path $WorktreePluginDir "plugin.json"
if (Test-Path $WorktreeTrackedPluginJson) {
    New-Item -ItemType HardLink -Path $WorktreePluginJson -Value $WorktreeTrackedPluginJson -Force | Out-Null
} else {
    $MainPluginJson = Join-Path $MainPluginDir "plugin.json"
    if (Test-Path $MainPluginJson) {
        New-Item -ItemType HardLink -Path $WorktreePluginJson -Value $MainPluginJson -Force | Out-Null
    }
}

# 10. Regenerate Dynamic Local Environment Config (unreal-instructions)
$EnvGenScript = Join-Path $WorktreePluginDir "src\generate_env_skill.ps1"
if (Test-Path $EnvGenScript) {
    Log-Message "Running generate_env_skill.ps1 inside worktree context to configure isolated paths..."
    & powershell.exe -ExecutionPolicy Bypass -File $EnvGenScript
    Log-Message "Local environment configuration completed successfully."
} else {
    Log-Message "Warning: Environment generation script not found at $EnvGenScript"
}

Log-Message "Worktree AgentFramework setup completed successfully."
