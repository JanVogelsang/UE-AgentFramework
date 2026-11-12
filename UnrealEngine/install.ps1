$DefaultProjectDir = "..\..\.."
$ProjectRoot = Read-Host "Enter the target project root directory [$DefaultProjectDir]"
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = $DefaultProjectDir
}

$PluginDir = $PSScriptRoot

# Try to resolve path
$TargetProjectDir = (Resolve-Path -Path "$PluginDir\$ProjectRoot" -ErrorAction SilentlyContinue).Path
if (-not $TargetProjectDir) {
    $TargetProjectDir = (Resolve-Path -Path $ProjectRoot -ErrorAction SilentlyContinue).Path
}
if (-not $TargetProjectDir) {
    $TargetProjectDir = $ProjectRoot
}
$TargetProjectDir = $TargetProjectDir.TrimEnd('\')

Write-Host "Installing to: $TargetProjectDir"

$AgentFrameworkPluginDir = "$TargetProjectDir\.agents\plugins\UnrealEngine"

# 0. Setup Unreal Engine C++ Plugin (AgentFramework)
$CppPluginSource = Join-Path (Get-Item $PluginDir).Parent.FullName "AgentFramework"
$CppPluginDest = "$TargetProjectDir\Plugins\AgentFramework"

Write-Host "Setting up Unreal Engine C++ Plugin (AgentFramework)..."
if (Test-Path $CppPluginSource) {
    if (-not (Test-Path $CppPluginDest)) {
        New-Item -ItemType Directory -Force -Path $CppPluginDest | Out-Null
    }
    Copy-Item -Path "$CppPluginSource\*" -Destination $CppPluginDest -Recurse -Force
    Write-Host "Copied AgentFramework C++ plugin to $CppPluginDest"
} else {
    Write-Host "Warning: AgentFramework source not found at $CppPluginSource"
}

# 1. Setup AgentFramework plugin directory
Write-Host "Setting up AgentFramework agent plugin..."
if (-not (Test-Path $AgentFrameworkPluginDir)) {
    New-Item -ItemType Directory -Force -Path $AgentFrameworkPluginDir | Out-Null
}
$ResolvedPluginDir = (Resolve-Path $PluginDir).Path
$ResolvedDestDir = (Resolve-Path $AgentFrameworkPluginDir).Path
if ($ResolvedPluginDir -ne $ResolvedDestDir) {
    Copy-Item -Path "$PluginDir\*" -Destination $AgentFrameworkPluginDir -Recurse -Force
}
# 2.5 Setup Workspace AGENTS.md
Write-Host "Setting up AGENTS.md at workspace root..."
$TargetAgentsPath = "$TargetProjectDir\AGENTS.md"
$SourceAgentsPath = "$PluginDir\AGENTS.md"
if (-not (Test-Path $SourceAgentsPath)) {
    $SourceAgentsPath = "$AgentFrameworkPluginDir\AGENTS.md"
}

if (Test-Path $SourceAgentsPath) {
    if (-not (Test-Path $TargetAgentsPath)) {
        Copy-Item -Path $SourceAgentsPath -Destination $TargetAgentsPath -Force
        Write-Host "Created AGENTS.md at workspace root."
    } else {
        $existingContent = Get-Content -Path $TargetAgentsPath -Raw
        if ($existingContent -notmatch "Agent Efficiency & Robustness Guidelines") {
            Add-Content -Path $TargetAgentsPath -Value "`n`n$(Get-Content -Path $SourceAgentsPath -Raw)"
            Write-Host "Appended Agent Efficiency & Robustness Guidelines to existing AGENTS.md."
        } else {
            Write-Host "AGENTS.md at workspace root already contains the efficiency guidelines."
        }
    }
}

# 3. Select AI coding assistants (multi-select)
Write-Host ""
Write-Host "Which AI coding assistants do you want to configure?"
Write-Host "  [0] Antigravity 2.0 (default)"
Write-Host "  [1] Kilo Code"
Write-Host "  [2] Claude Code"
Write-Host "  [3] OpenAI Codex"
Write-Host "You can select multiple assistants, e.g. '0,2' or 'all'."
Write-Host "Re-running this script later lets you add additional assistants."
$clientChoice = Read-Host "Select (comma-separated, press Enter for Antigravity 2.0)"

if ([string]::IsNullOrWhiteSpace($clientChoice)) {
    $selections = @("0")
} elseif ($clientChoice.Trim() -ieq "all") {
    $selections = @("0", "1", "2", "3")
} else {
    $selections = @($clientChoice -split '[,;\s]+' | Where-Object { $_ -ne "" })
}

$validChoices = @("0", "1", "2", "3")
$unknown = @($selections | Where-Object { $validChoices -notcontains $_ })
if ($unknown.Count -gt 0) {
    Write-Host "Warning: Ignoring unknown selection(s): $($unknown -join ', ')"
}
$selections = @($selections | Where-Object { $validChoices -contains $_ })
if ($selections.Count -eq 0) {
    $selections = @("0")
    Write-Host "No valid selection provided. Defaulting to Antigravity 2.0."
}

$useAntigravity = ($selections -contains "0")
$useKiloCode    = ($selections -contains "1")
$useClaudeCode  = ($selections -contains "2")
$useCodex       = ($selections -contains "3")

# Resolve dynamic python path
$UserDir = $env:USERPROFILE
$StorePython = "$UserDir\AppData\Local\Microsoft\WindowsApps\python.exe"
if (Test-Path $StorePython) {
    $PythonExe = $StorePython
} else {
    $PythonExe = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $PythonExe) { $PythonExe = "python" }
}

# Setup dedicated virtual environment for External Server in destination
Write-Host "Setting up Python virtual environment..."
$VenvDir = "$AgentFrameworkPluginDir\ExternalServer\.venv"
if (-not (Test-Path $VenvDir)) {
    & $PythonExe -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to create Python virtual environment."
        exit 1
    }
}
$VenvPython = "$VenvDir\Scripts\python.exe"
$PythonExeEscaped = $VenvPython -replace '\\', '\\'

# Install External Server runtime dependencies via pip
Write-Host "Installing External Server dependencies via pip..."
& $VenvPython -m pip install -r "$AgentFrameworkPluginDir\ExternalServer\requirements.txt"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to install Python dependencies via pip. Setup process failed."
    exit 1
}

Write-Host "Installing Bridge dependencies via pip..."
& $VenvPython -m pip install -r "$AgentFrameworkPluginDir\bridge\requirements.txt"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to install Python dependencies via pip for bridge. Setup process failed."
    exit 1
}

# Resolve repository root dynamically (parent of the UnrealEngine folder)
$RepoRoot = (Get-Item $PluginDir).Parent.FullName -replace '\\', '\\'
$PluginDirEscaped = $AgentFrameworkPluginDir -replace '\\', '\\'

# Resolve the skills source directory once for all assistants
$SkillsSourceDir = "$AgentFrameworkPluginDir\skills"
if (-not (Test-Path $SkillsSourceDir)) { $SkillsSourceDir = "$PluginDir\skills" }

# ── Resolve Unreal Engine tool paths (used to pre-authorize editor workflows per assistant) ──
$EngineAssociation = "5.9"
$Uproject = Get-ChildItem -Path $TargetProjectDir -Filter *.uproject -ErrorAction SilentlyContinue | Select-Object -First 1
if ($Uproject) {
    try {
        $UprojectJson = Get-Content -Raw -Path $Uproject.FullName | ConvertFrom-Json
        if ($UprojectJson.EngineAssociation) { $EngineAssociation = $UprojectJson.EngineAssociation }
    } catch {}
}
$EngineRoot = $null
if ($EngineAssociation -match "^\d+\.\d+$") {
    $EngineRoot = (Get-ItemProperty -Path "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$EngineAssociation" -Name InstalledDirectory -ErrorAction SilentlyContinue).InstalledDirectory
}
if (-not $EngineRoot) {
    $BuildsKey = "HKCU:\Software\Epic Games\Unreal Engine\Builds"
    if (Test-Path $BuildsKey) {
        $EngineRoot = (Get-ItemProperty -Path $BuildsKey -Name $EngineAssociation -ErrorAction SilentlyContinue).$EngineAssociation
    }
}
if (-not $EngineRoot) {
    foreach ($p in @("D:\UE_$EngineAssociation", "C:\Program Files\Epic Games\UE_$EngineAssociation", "C:\Program Files (x86)\Epic Games\UE_$EngineAssociation", "D:\Epic Games\UE_$EngineAssociation")) {
        if (Test-Path $p) { $EngineRoot = $p; break }
    }
}
$EditorExe = $null; $EditorCmdExe = $null; $BuildBat = $null
if ($EngineRoot) {
    $EngineRoot = $EngineRoot.TrimEnd('\')
    $EditorExe = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe"
    $EditorCmdExe = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    $BuildBat = "$EngineRoot\Engine\Build\BatchFiles\Build.bat"
}
$DevAuthToolExe = "C:\Program Files\EOS_DevAuthTool\EOS_DevAuthTool.exe"

# Hard-links each skill's SKILL.md into the given directory.
# Flat mode: <dir>\<skill>.md (Kilo rules). Nested mode: <dir>\<skill>\SKILL.md (Claude/Codex).
function Install-SkillLinks {
    param(
        [string]$SourceDir,
        [string]$TargetDir,
        [switch]$Nested
    )
    if (-not (Test-Path $TargetDir)) {
        New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    }
    Get-ChildItem -Path $SourceDir -Directory | ForEach-Object {
        $SkillName = $_.Name
        $SourceSkill = Join-Path $_.FullName "SKILL.md"
        if ($Nested) {
            $TargetSkillDir = Join-Path $TargetDir $SkillName
            if (-not (Test-Path $TargetSkillDir)) {
                New-Item -ItemType Directory -Force -Path $TargetSkillDir | Out-Null
            }
            $TargetRule = Join-Path $TargetSkillDir "SKILL.md"
        } else {
            $TargetRule = Join-Path $TargetDir "$SkillName.md"
        }
        if (Test-Path $TargetRule) { Remove-Item $TargetRule }
        if (Test-Path $SourceSkill) {
            New-Item -ItemType HardLink -Path $TargetRule -Value $SourceSkill | Out-Null
            Write-Host "  Linked skill '$SkillName'."
        }
    }
}

if ($useKiloCode) {
    # ── Kilo Code ────────────────────────────────────────────────────────────

    # Ask for LLM profile
    Write-Host ""
    Write-Host "Available LLM profiles for Kilo Code:"
    $profiles = Get-ChildItem -Path "$PluginDir\profiles\*.json" | ForEach-Object { $_.BaseName }
    for ($i = 0; $i -lt $profiles.Count; $i++) {
        Write-Host "  [$i] $($profiles[$i])"
    }
    $profileChoice = Read-Host "Select a profile (enter number, or press Enter for 'default')"
    $selectedProfile = "default"
    if (-not [string]::IsNullOrWhiteSpace($profileChoice)) {
        $idx = [int]$profileChoice
        if ($idx -ge 0 -and $idx -lt $profiles.Count) {
            $selectedProfile = $profiles[$idx]
        }
    }
    Write-Host "Using profile: $selectedProfile"

    # Setup Kilo Code rules (skill hard-links)
    $KiloCodeRulesDir = "$TargetProjectDir\.kilocode\rules"
    Write-Host "Setting up Kilo Code rules..."
    Install-SkillLinks -SourceDir $SkillsSourceDir -TargetDir $KiloCodeRulesDir

    # Write kilo.jsonc
    $KiloConfigPath = "$TargetProjectDir\kilo.jsonc"
    $KiloConfigContent = @"
{
  // Pre-approve Unreal developer workflows so the agent does not prompt for the
  // editor launch, headless builds, automation tests, or the EOS DevAuthTool.
  // Kilo matches these glob patterns against the command string; unmatched
  // commands still follow your default approval behavior.
  "permission": {
    "bash": {
      "*UnrealEditor.exe*": "allow",
      "*UnrealEditor-Cmd.exe*": "allow",
      "*Build.bat*": "allow",
      "*EOS_DevAuthTool.exe*": "allow"
    }
  },
  "mcpServers": {
    "unrealengine": {
      "type": "stdio",
      "command": "$PythonExeEscaped",
      "args": ["-X", "utf8", "-u", "-m", "bridge.main"],
      "env": {
        "BRIDGE_PROFILE": "$selectedProfile",
        "PYTHONPATH": "$PluginDirEscaped"
      },
      "enabled": true
    },
    "cpp-ast-rag": {
      "type": "stdio",
      "command": "$PythonExeEscaped",
      "args": ["-u", "-m", "ExternalServer.src.main"],
      "env": {
        "PYTHONPATH": "$PluginDirEscaped"
      },
      "enabled": true
    }
  }
}
"@

    if (-not (Test-Path $KiloConfigPath)) {
        Set-Content -Path $KiloConfigPath -Value $KiloConfigContent
        Write-Host "Created kilo.jsonc."
    } elseif ((Get-Content -Path $KiloConfigPath -Raw) -match '"unrealengine"') {
        Write-Host "kilo.jsonc already contains the unrealengine MCP server. Leaving it untouched."
    } else {
        Write-Host "kilo.jsonc already exists. Please manually merge the MCP server definition:"
        Write-Host $KiloConfigContent
    }

    Write-Host ""
    Write-Host "Installation Complete (Kilo Code)."
    Write-Host "  AgentFramework plugin : $AgentFrameworkPluginDir"
    Write-Host "  Kilo Code rules    : $KiloCodeRulesDir"
    Write-Host "  LLM Profile        : $selectedProfile"
}

if ($useClaudeCode) {
    # ── Claude Code ──────────────────────────────────────────────────────────

    # Setup Claude Code skills (skill hard-links)
    $ClaudeSkillsDir = "$TargetProjectDir\.claude\skills"
    Write-Host "Setting up Claude Code skills..."
    Install-SkillLinks -SourceDir $SkillsSourceDir -TargetDir $ClaudeSkillsDir -Nested

    # Setup Workspace CLAUDE.md by copying AGENTS.md
    Write-Host "Setting up CLAUDE.md at workspace root..."
    $TargetClaudePath = "$TargetProjectDir\CLAUDE.md"
    if (Test-Path $SourceAgentsPath) {
        if (-not (Test-Path $TargetClaudePath)) {
            Copy-Item -Path $SourceAgentsPath -Destination $TargetClaudePath -Force
            Write-Host "Created CLAUDE.md at workspace root."
        } else {
            $existingContent = Get-Content -Path $TargetClaudePath -Raw
            if ($existingContent -notmatch "Agent Efficiency & Robustness Guidelines") {
                Add-Content -Path $TargetClaudePath -Value "`n`n$(Get-Content -Path $SourceAgentsPath -Raw)"
                Write-Host "Appended Agent Efficiency & Robustness Guidelines to existing CLAUDE.md."
            } else {
                Write-Host "CLAUDE.md at workspace root already contains the efficiency guidelines."
            }
        }
    }

    # Write .mcp.json
    $McpConfigPath = "$TargetProjectDir\.mcp.json"
    $McpConfigContent = @"
{
  "mcpServers": {
    "unrealengine": {
      "command": "$PythonExeEscaped",
      "args": ["-X", "utf8", "-u", "-m", "bridge.main"],
      "env": {
        "PYTHONPATH": "$PluginDirEscaped"
      }
    },
    "cpp-ast-rag": {
      "command": "$PythonExeEscaped",
      "args": ["-u", "-m", "ExternalServer.src.main"],
      "env": {
        "PYTHONPATH": "$PluginDirEscaped"
      }
    }
  }
}
"@

    if (-not (Test-Path $McpConfigPath)) {
        Set-Content -Path $McpConfigPath -Value $McpConfigContent
        Write-Host "Created .mcp.json."
    } elseif ((Get-Content -Path $McpConfigPath -Raw) -match '"unrealengine"') {
        Write-Host ".mcp.json already contains the unrealengine MCP server. Leaving it untouched."
    } else {
        Write-Host ".mcp.json already exists. Please manually merge the MCP server definition:"
        Write-Host $McpConfigContent
    }

    # Pre-authorize Unreal developer workflows (editor launch, builds, automation tests)
    # via permission allow rules in the project's shared .claude\settings.json.
    # Rules are matched per-subcommand by Claude Code, so compound-command injection does not widen them.
    $ClaudeSettingsPath = "$TargetProjectDir\.claude\settings.json"
    $UnrealAllowRules = @(
        "PowerShell(*UnrealEditor.exe*)",
        "PowerShell(*UnrealEditor-Cmd.exe*)",
        "PowerShell(*\Build\BatchFiles\Build.bat*)",
        "PowerShell(*EOS_DevAuthTool.exe*)",
        "Bash(*UnrealEditor.exe*)",
        "Bash(*UnrealEditor-Cmd.exe*)",
        "Bash(*/Build/BatchFiles/Build.bat*)",
        "Bash(*EOS_DevAuthTool.exe*)"
    )
    $ClaudeSettingsWritten = $false
    if (-not (Test-Path $ClaudeSettingsPath)) {
        $SettingsObj = [ordered]@{ permissions = [ordered]@{ allow = $UnrealAllowRules } }
        Set-Content -Path $ClaudeSettingsPath -Value ($SettingsObj | ConvertTo-Json -Depth 5)
        Write-Host "Created .claude\settings.json with Unreal workflow allow rules."
        $ClaudeSettingsWritten = $true
    } else {
        try {
            $SettingsObj = Get-Content -Raw -Path $ClaudeSettingsPath | ConvertFrom-Json
            if (-not $SettingsObj.permissions) {
                $SettingsObj | Add-Member -NotePropertyName permissions -NotePropertyValue ([pscustomobject]@{ allow = @() })
            }
            if (-not $SettingsObj.permissions.allow) {
                $SettingsObj.permissions | Add-Member -NotePropertyName allow -NotePropertyValue @() -Force
            }
            $ExistingRules = @($SettingsObj.permissions.allow)
            $NewRules = @($UnrealAllowRules | Where-Object { $ExistingRules -notcontains $_ })
            if ($NewRules.Count -gt 0) {
                $SettingsObj.permissions.allow = $ExistingRules + $NewRules
                Set-Content -Path $ClaudeSettingsPath -Value ($SettingsObj | ConvertTo-Json -Depth 10)
                Write-Host "Merged Unreal workflow allow rules into existing .claude\settings.json."
            } else {
                Write-Host ".claude\settings.json already contains the Unreal workflow allow rules."
            }
            $ClaudeSettingsWritten = $true
        } catch {
            Write-Host "Warning: Could not parse existing .claude\settings.json. Please add these permission allow rules manually:"
            $UnrealAllowRules | ForEach-Object { Write-Host "    $_" }
        }
    }

    Write-Host ""
    Write-Host "Installation Complete (Claude Code)."
    Write-Host "  AgentFramework plugin : $AgentFrameworkPluginDir"
    Write-Host "  Claude config      : $McpConfigPath"
    Write-Host "  Claude rules       : $TargetClaudePath"
    if ($ClaudeSettingsWritten) {
        Write-Host "  Claude permissions : $ClaudeSettingsPath"
    }
}

if ($useCodex) {
    # ── OpenAI Codex ─────────────────────────────────────────────────────────

    # Setup Codex skills (skill hard-links)
    $CodexSkillsDir = "$TargetProjectDir\.codex\skills"
    Write-Host "Setting up OpenAI Codex skills..."
    Install-SkillLinks -SourceDir $SkillsSourceDir -TargetDir $CodexSkillsDir -Nested

    # Write .codex/config.toml
    $CodexConfigDir = "$TargetProjectDir\.codex"
    if (-not (Test-Path $CodexConfigDir)) {
        New-Item -ItemType Directory -Force -Path $CodexConfigDir | Out-Null
    }
    $CodexConfigPath = "$CodexConfigDir\config.toml"
    $CodexConfigContent = @"
[mcp_servers.unrealengine]
command = "$PythonExeEscaped"
args = ["-X", "utf8", "-u", "-m", "bridge.main"]

[mcp_servers.unrealengine.env]
PYTHONPATH = "$PluginDirEscaped"

[mcp_servers.cpp-ast-rag]
command = "$PythonExeEscaped"
args = ["-u", "-m", "ExternalServer.src.main"]

[mcp_servers.cpp-ast-rag.env]
PYTHONPATH = "$PluginDirEscaped"
"@

    if (-not (Test-Path $CodexConfigPath)) {
        Set-Content -Path $CodexConfigPath -Value $CodexConfigContent
        Write-Host "Created .codex/config.toml."
    } elseif ((Get-Content -Path $CodexConfigPath -Raw) -match 'mcp_servers\.unrealengine') {
        Write-Host ".codex/config.toml already contains the unrealengine MCP server. Leaving it untouched."
    } else {
        Write-Host ".codex/config.toml already exists. Please manually merge the MCP server definition:"
        Write-Host $CodexConfigContent
    }

    # Pre-authorize Unreal developer workflows via a Codex execpolicy rules file.
    # Codex matches prefix rules against the argv of a directly-invoked program, so
    # these cover direct build/automation-test/editor invocations. The rules layer
    # loads only after you trust the project. Commands wrapped in a shell one-liner
    # (e.g. a detached editor launch via Invoke-CimMethod) still follow approval_policy.
    $CodexRulesDir = "$CodexConfigDir\rules"
    if (-not (Test-Path $CodexRulesDir)) {
        New-Item -ItemType Directory -Force -Path $CodexRulesDir | Out-Null
    }
    $CodexRulesPath = "$CodexRulesDir\unreal.rules"
    if (-not (Test-Path $CodexRulesPath)) {
        $CodexRuleLines = @(
            "# Auto-generated by UE-AgentFramework install.ps1.",
            "# Allow Unreal Engine developer executables to run without an approval prompt.",
            "# 'decision' may be `"allow`", `"prompt`", or `"forbidden`". Edit or extend as needed."
        )
        foreach ($exe in @($BuildBat, $EditorExe, $EditorCmdExe, $DevAuthToolExe)) {
            if ($exe -and (Test-Path $exe)) {
                $exeStarlark = $exe -replace '\\', '\\'
                $CodexRuleLines += ""
                $CodexRuleLines += "prefix_rule("
                $CodexRuleLines += "    pattern = [`"$exeStarlark`"],"
                $CodexRuleLines += "    decision = `"allow`","
                $CodexRuleLines += "    justification = `"Unreal Engine developer workflow (UE-AgentFramework).`","
                $CodexRuleLines += ")"
            }
        }
        Set-Content -Path $CodexRulesPath -Value ($CodexRuleLines -join "`n") -Encoding UTF8
        Write-Host "Created .codex/rules/unreal.rules execpolicy."
    } else {
        Write-Host ".codex/rules/unreal.rules already exists. Leaving it untouched."
    }

    Write-Host ""
    Write-Host "Installation Complete (OpenAI Codex)."
    Write-Host "  AgentFramework plugin : $AgentFrameworkPluginDir"
    Write-Host "  Codex config       : $CodexConfigPath"
    Write-Host "  Codex execpolicy   : $CodexRulesPath"
}

if ($useAntigravity) {
    # ── Antigravity 2.0 (default) ────────────────────────────────────────────

    $McpConfigContent = @"
{
  "mcpServers": {
    "unrealengine": {
      "type": "stdio",
      "command": "$PythonExeEscaped",
      "args": ["-X", "utf8", "-u", "-m", "bridge.main"],
      "env": {
        "PYTHONPATH": "$PluginDirEscaped"
      },
      "enabled": true
    },
    "cpp-ast-rag": {
      "type": "stdio",
      "command": "$PythonExeEscaped",
      "args": ["-u", "-m", "ExternalServer.src.main"],
      "env": {
        "PYTHONPATH": "$PluginDirEscaped"
      },
      "enabled": true
    }
  }
}
"@

    $PluginConfigPath = Join-Path $AgentFrameworkPluginDir "mcp_config.json"
    Set-Content -Path $PluginConfigPath -Value $McpConfigContent
    Write-Host "Created/Updated mcp_config.json."

    # Pre-authorize Unreal developer workflows in Antigravity's global permission grants.
    # This is the same file that the `ask_permission` tool writes to at runtime, so
    # pre-seeding the `unsandboxed(<editor>)` grant plus the launch/build command
    # prefixes lets the editor start without the initial approval prompt.
    $AntigravityConfigPath = "$env:USERPROFILE\.gemini\config\config.json"
    $AntigravityGrants = @("command(Invoke-CimMethod)", "command(Start-Process)")
    if ($EditorExe) { $AntigravityGrants += "unsandboxed($EditorExe)" }
    if (Test-Path $AntigravityConfigPath) {
        try {
            $AgConfig = Get-Content -Raw -Path $AntigravityConfigPath | ConvertFrom-Json
            if (-not $AgConfig.userSettings) {
                $AgConfig | Add-Member -NotePropertyName userSettings -NotePropertyValue ([pscustomobject]@{}) -Force
            }
            if (-not $AgConfig.userSettings.globalPermissionGrants) {
                $AgConfig.userSettings | Add-Member -NotePropertyName globalPermissionGrants -NotePropertyValue ([pscustomobject]@{ allow = @() }) -Force
            }
            if (-not $AgConfig.userSettings.globalPermissionGrants.allow) {
                $AgConfig.userSettings.globalPermissionGrants | Add-Member -NotePropertyName allow -NotePropertyValue @() -Force
            }
            $ExistingGrants = @($AgConfig.userSettings.globalPermissionGrants.allow)
            $NewGrants = @($AntigravityGrants | Where-Object { $ExistingGrants -notcontains $_ })
            if ($NewGrants.Count -gt 0) {
                # Back up before touching the user's global config, then append (never remove/reorder).
                Copy-Item -Path $AntigravityConfigPath -Destination "$AntigravityConfigPath.bak" -Force
                $AgConfig.userSettings.globalPermissionGrants.allow = $ExistingGrants + $NewGrants
                Set-Content -Path $AntigravityConfigPath -Value ($AgConfig | ConvertTo-Json -Depth 20) -Encoding UTF8
                Write-Host "Seeded Antigravity global permission grants (backup at config.json.bak):"
                $NewGrants | ForEach-Object { Write-Host "    + $_" }
            } else {
                Write-Host "Antigravity global permission grants already cover the Unreal workflows."
            }
        } catch {
            Write-Host "Warning: Could not update Antigravity global config ($AntigravityConfigPath)."
            Write-Host "The agent will still prompt once via ask_permission on first editor launch, which is expected."
        }
    } else {
        Write-Host "Note: Antigravity global config not found yet. The agent will prompt once via"
        Write-Host "ask_permission on first editor launch and persist the grant automatically."
    }

    Write-Host ""
    Write-Host "Installation Complete (Antigravity 2.0)."
    Write-Host "  AgentFramework plugin : $AgentFrameworkPluginDir"
    Write-Host "  MCP config         : $PluginConfigPath"
}

# 4. Setup Git Worktree Hooks (Recommended)
Write-Host "Setting up Git Worktree Hooks..."
$TargetGithooksDir = "$TargetProjectDir\.githooks"
$SourceGithooksDir = "$PluginDir\.githooks"
if (-not (Test-Path $SourceGithooksDir)) {
    $SourceGithooksDir = "$AgentFrameworkPluginDir\.githooks"
}

if (Test-Path $SourceGithooksDir) {
    if (-not (Test-Path $TargetGithooksDir)) {
        New-Item -ItemType Directory -Force -Path $TargetGithooksDir | Out-Null
    }

    # 1. Copy the PowerShell logic script (which has a unique name post-checkout.ps1)
    $SourcePsHook = Join-Path $SourceGithooksDir "post-checkout.ps1"
    $TargetPsHook = Join-Path $TargetGithooksDir "post-checkout.ps1"
    if (Test-Path $SourcePsHook) {
        Copy-Item -Path $SourcePsHook -Destination $TargetPsHook -Force
    }

    # 2. Safely merge the bash wrapper
    $SourceBashHook = Join-Path $SourceGithooksDir "post-checkout"
    $TargetBashHook = Join-Path $TargetGithooksDir "post-checkout"
    
    $HookInvocation = "`npowershell.exe -ExecutionPolicy Bypass -File `"`$(dirname `"`$0`")/post-checkout.ps1`" `"`$1`" `"`$2`" `"`$3`""

    if (-not (Test-Path $TargetBashHook)) {
        Copy-Item -Path $SourceBashHook -Destination $TargetBashHook -Force
        Write-Host "Created post-checkout hook wrapper."
    } else {
        $ExistingContent = Get-Content -Path $TargetBashHook -Raw
        if ($ExistingContent -notmatch "post-checkout.ps1") {
            Add-Content -Path $TargetBashHook -Value $HookInvocation
            Write-Host "Appended AgentFramework worktree hook to existing post-checkout hook."
        } else {
            Write-Host "Existing post-checkout hook already contains AgentFramework configuration."
        }
    }

    # 3. Verify if target project is a Git repository
    if (Test-Path "$TargetProjectDir\.git") {
        Push-Location $TargetProjectDir
        
        # Check if they already have a custom hooksPath configured
        $CurrentHooksPath = (git config core.hooksPath) 2>$null
        if ($null -eq $CurrentHooksPath -or $CurrentHooksPath -eq "" -or $CurrentHooksPath -eq ".githooks") {
            git config core.hooksPath .githooks
            Write-Host "Configured target Git repository to use .githooks folder."
        } else {
            Write-Host "Warning: Target project already has a custom core.hooksPath configured ($CurrentHooksPath)."
            Write-Host "To enable AgentFramework worktrees, please manually add the post-checkout hook to: $CurrentHooksPath"
        }
        Pop-Location
    } else {
        Write-Host "Note: Target project does not have a local .git repository. Git hooks path was not configured automatically."
    }
}

# 5. Append .env to .gitignore if target project is a Git repository
$gitignore = Join-Path $TargetProjectDir ".gitignore"
$envLine = ".env"
if (Test-Path $gitignore) {
    $content = Get-Content $gitignore -Raw
    if ($content -notmatch '(?m)^\.env$') {
        if ($content -and $content[-1] -ne "`n") {
            Add-Content $gitignore ""
        }
        Add-Content $gitignore $envLine
        Write-Host "Appended .env to target project .gitignore."
    }
} else {
    Set-Content $gitignore $envLine
    Write-Host "Created target project .gitignore and added .env."
}

$configuredAssistants = @()
if ($useAntigravity) { $configuredAssistants += "Antigravity 2.0" }
if ($useKiloCode)    { $configuredAssistants += "Kilo Code" }
if ($useClaudeCode)  { $configuredAssistants += "Claude Code" }
if ($useCodex)       { $configuredAssistants += "OpenAI Codex" }

Write-Host ""
Write-Host "------------------------------------------------------------------------"
Write-Host "Configured assistants: $($configuredAssistants -join ', ')"
Write-Host "(Re-run this script anytime to add additional assistants.)"
Write-Host ""
Write-Host "Next Step (Highly Recommended):"
Write-Host "  Ensure Unreal Editor is running, open a new conversation with your"
Write-Host "  AI assistant (which will follow the installed rules/skills), and ask to:"
Write-Host ""
Write-Host "    'Run project setup and index the project'"
Write-Host ""
Write-Host "  This will scan your project structures, configure your local environment inside"
Write-Host "  'unreal-instructions', and build a permanent OKF project-index skill to optimize"
Write-Host "  all subsequent agent conversations."
Write-Host "------------------------------------------------------------------------"

