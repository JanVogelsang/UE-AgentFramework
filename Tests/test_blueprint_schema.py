import json
import re
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def test_uplugin_targets_ue59_and_enables_native_toolsets():
    """Verify AgentFramework.uplugin targets UE 5.9.0 and enables verified UE 5.9 plugins."""
    uplugin_path = PROJECT_ROOT / "AgentFramework" / "AgentFramework.uplugin"
    assert uplugin_path.exists()

    data = json.loads(uplugin_path.read_text(encoding="utf-8"))
    assert data.get("EngineVersion") == "5.9.0"
    assert data.get("VersionName") == "2.0.0"
    assert "6 native ToolsetRegistry toolsets" in data.get("Description", "")

    plugins = {p["Name"]: p for p in data.get("Plugins", [])}
    required_plugins = ["ModelContextProtocol", "ToolsetRegistry"]
    for plugin_name in required_plugins:
        assert plugin_name in plugins, f"Missing required UE 5.9 plugin '{plugin_name}'"
        assert plugins[plugin_name].get("Enabled") is True

    optional_ue59_plugins = [
        "AllToolsets",
        "GeometryToolset",
        "KnowledgeBaseToolset",
        "SequencerAnimMixerToolset",
        "AIAssistant",
        "AgentCapability",
        "AgentLook",
        "EditorScriptingUtilities",
        "EnhancedInput",
        "PythonScriptPlugin",
        "GameplayAbilities",
        "StateTree",
        "SmartObjects",
        "MassGameplay",
        "MassCrowd",
        "UnrealCanvasAnnotations",
        "UnrealCanvasBridge",
    ]
    for plugin_name in optional_ue59_plugins:
        assert plugin_name in plugins, f"Missing optional UE 5.9 plugin '{plugin_name}'"
        assert plugins[plugin_name].get("Enabled") is True
        assert plugins[plugin_name].get("Optional") is True

    removed_nonexistent_plugins = [
        "LogsToolset",
        "MaterialToolsets",
        "ObjectToolset",
        "SubobjectToolset",
        "UserDefinedStructureToolset",
    ]
    for plugin_name in removed_nonexistent_plugins:
        assert plugin_name not in plugins, f"Non-existent plugin '{plugin_name}' should not be in .uplugin"


def test_six_ue59_toolset_headers_and_sources_exist():
    """Verify all 6 UE 5.9 UToolsetDefinition headers/sources/tools exist and legacy UE 5.8 files are removed."""
    public_dir = PROJECT_ROOT / "AgentFramework" / "Source" / "AgentFrameworkActions" / "Public" / "Toolsets"
    private_dir = PROJECT_ROOT / "AgentFramework" / "Source" / "AgentFrameworkActions" / "Private" / "Toolsets"

    expected_toolsets = {
        "AgentFrameworkCppToolset": (
            "UAgentFrameworkCppToolset",
            ["CreateCppClass", "ScaffoldCppClass", "RegenerateProjectFiles", "GetCppReflectionInfo"],
        ),
        "AgentFrameworkBuildToolset": (
            "UAgentFrameworkBuildToolset",
            ["BuildLighting", "PackageProject"],
        ),
        "AgentFrameworkEnhancedInputToolset": (
            "UAgentFrameworkEnhancedInputAuthoringToolset",
            ["CreateInputAction", "CreateInputMappingContext", "AddInputMapping", "ConfigureInputMappingModifiersTriggers"],
        ),
        "AgentFrameworkAnimationToolset": (
            "UAgentFrameworkAnimationAuthoringToolset",
            [
                "CreateAnimBlueprint",
                "CreateAnimMontage",
                "ConfigureAnimMontage",
                "CreateBlendSpace",
                "GetSkeletonAnimationInfo",
                "CreateIKRig",
                "CreateIKRetargeter",
                "ConfigureMotionMatchingDatabase",
            ],
        ),
        "AgentFrameworkGASToolset": (
            "UAgentFrameworkGASAuthoringToolset",
            ["CreateAttributeSetClass", "CreateGameplayEffectBlueprint"],
        ),
        "AgentFrameworkPIEToolset": (
            "UAgentFrameworkPIERuntimeToolset",
            ["CallPIEActorFunction"],
        ),
    }

    for file_stem, (class_name, expected_tools) in expected_toolsets.items():
        header = public_dir / f"{file_stem}.h"
        source = private_dir / f"{file_stem}.cpp"
        assert header.exists(), f"Missing toolset header: {header}"
        assert source.exists(), f"Missing toolset source: {source}"
        header_text = header.read_text(encoding="utf-8")
        assert "UToolsetDefinition" in header_text
        assert class_name in header_text
        assert "AICallable" in header_text
        for tool_name in expected_tools:
            assert re.search(rf"\b{re.escape(tool_name)}\s*\(", header_text), (
                f"Expected tool '{tool_name}' missing from {header.name}"
            )

    # Verify deleted MassAI/SmartObjects toolset, legacy port 18777 HTTP server, and static ToolSchemas are gone
    assert not (public_dir / "AgentFrameworkMassAIToolset.h").exists()
    assert not (private_dir / "AgentFrameworkMassAIToolset.cpp").exists()
    legacy_http = PROJECT_ROOT / "AgentFramework" / "Source" / "AgentFrameworkActions" / "Public" / "AgentFrameworkHttpServer.h"
    legacy_schemas = PROJECT_ROOT / "AgentFramework" / "Resources" / "ToolSchemas"
    assert not legacy_http.exists(), "Legacy AgentFrameworkHttpServer.h should be removed in UE 5.9"
    assert not legacy_schemas.exists(), "Legacy static ToolSchemas directory should be removed in UE 5.9"
