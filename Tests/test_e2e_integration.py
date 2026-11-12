import json
import uuid

import pytest

EXPECTED_AGENTFRAMEWORK_TOOLSETS = [
    "AgentFrameworkActions.AgentFrameworkCppToolset",
    "AgentFrameworkActions.AgentFrameworkBuildToolset",
    "AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset",
    "AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset",
    "AgentFrameworkActions.AgentFrameworkGASAuthoringToolset",
    "AgentFrameworkActions.AgentFrameworkPIERuntimeToolset",
]


def _is_error_response(response: dict) -> bool:
    if not isinstance(response, dict):
        return True
    if "error" in response:
        return True
    result = response.get("result", {})
    return isinstance(result, dict) and result.get("isError") is True


def _extract_text(response: dict) -> str:
    if "error" in response:
        return json.dumps(response["error"])
    result = response.get("result", {})
    texts = [
        item.get("text", "")
        for item in result.get("content", [])
        if isinstance(item, dict) and item.get("type") == "text"
    ]
    return "\n".join(texts)


def _extract_return_value(response: dict):
    assert not _is_error_response(response), f"Expected MCP success, got: {response}"
    text = _extract_text(response).strip()
    if not text:
        return {}
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        return text
    if isinstance(parsed, dict) and "returnValue" in parsed:
        return parsed["returnValue"]
    return parsed


def test_python_mcp_query_cpp_ast(mock_agent_client):
    """Test Python ExternalServer MCP tool: query_cpp_ast."""
    response = mock_agent_client.call_python_tool(
        "query_cpp_ast",
        {"query": "class AMorphTargetActor"},
    )
    assert response is not None
    assert "result" in response
    result = response["result"]
    assert "content" in result
    assert len(result["content"]) > 0
    text_content = result["content"][0]["text"]
    assert "query_cpp_ast" in text_content
    assert "AMorphTargetActor" in text_content


def test_python_mcp_generate_compile_commands(mock_agent_client):
    """Test Python ExternalServer MCP tool: generate_compile_commands."""
    response = mock_agent_client.call_python_tool(
        "generate_compile_commands",
        {},
    )
    assert response is not None
    assert "result" in response
    result = response["result"]
    assert "content" in result
    assert len(result["content"]) > 0
    assert "Compilation generation started in background" in result["content"][0]["text"]


def test_python_mcp_search_vector_db(mock_agent_client):
    """Test Python ExternalServer MCP tool: search_vector_db."""
    response = mock_agent_client.call_python_tool(
        "search_vector_db",
        {"query": "character movement replication"},
    )
    assert response is not None
    assert "result" in response
    result = response["result"]
    assert "content" in result
    assert len(result["content"]) > 0
    assert "search_vector_db" in result["content"][0]["text"]


def test_ue59_mcp_tool_discovery_and_agentframework_toolsets(mock_agent_client):
    """Verify UE 5.9 /mcp exposes all 6 AgentFrameworkActions toolsets (ToolSearch or direct mode)."""
    top_level_tools = mock_agent_client.list_cpp_tools()
    top_level_names = {t.get("name") for t in top_level_tools if isinstance(t, dict)}
    assert len(top_level_names) > 0, "Expected UE 5.9 /mcp tools/list to return tools"

    if "list_toolsets" in top_level_names:
        # ToolSearch enabled (default in UE 5.9)
        list_resp = mock_agent_client.call_cpp_tool("list_toolsets", {})
        payload_text = _extract_text(list_resp)
        for toolset_name in EXPECTED_AGENTFRAMEWORK_TOOLSETS:
            assert toolset_name in payload_text, f"Missing registered UE 5.9 toolset: {toolset_name}"

        if "describe_toolsets" in top_level_names:
            desc_resp = mock_agent_client.call_cpp_tool(
                "describe_toolsets",
                {"toolset_names": ["AgentFrameworkActions.AgentFrameworkCppToolset"]},
            )
            desc_text = _extract_text(desc_resp)
            for expected_tool in ("CreateCppClass", "ScaffoldCppClass", "RegenerateProjectFiles", "GetCppReflectionInfo"):
                assert expected_tool in desc_text
    else:
        # Direct mode (bEnableToolSearch = false)
        assert "AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo" in top_level_names


def test_ue59_cpp_reflection_info(mock_agent_client):
    """Test GetCppReflectionInfo on UE 5.9 /mcp via ToolSearch call_tool or direct invocation."""
    response = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo",
        {"TypeName": "Actor", "bIncludeInherited": False},
    )
    info = _extract_return_value(response)
    assert isinstance(info, dict)
    assert info.get("TypeName") == "Actor"
    assert info.get("TypeKind") in ("Class", "UClass")
    assert isinstance(info.get("Properties"), list)
    assert isinstance(info.get("Functions"), list)


def test_ue59_cpp_reflection_unknown_type_returns_error(mock_agent_client):
    """Verify missing type triggers a clean JSON-RPC or isError failure instead of crashing."""
    response = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo",
        {"TypeName": "NonExistentClassXYZ_UE59", "bIncludeInherited": False},
    )
    assert _is_error_response(response), f"Expected error for nonexistent type, got: {response}"


def test_ue59_toolset_validation_guards(mock_agent_client):
    """Verify negative/validation error paths across retained UE 5.9 toolsets without modifying project files."""
    # Invalid C++ identifier
    bad_cpp = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkCppToolset.CreateCppClass",
        {"ClassName": "123InvalidActor", "ParentClassName": "Actor"},
    )
    assert _is_error_response(bad_cpp)

    # Invalid BuildLighting quality
    bad_lighting = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkBuildToolset.BuildLighting",
        {"Quality": "InvalidLightingQuality99"},
    )
    assert _is_error_response(bad_lighting)

    # Invalid GameplayEffect path
    bad_ge = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkGASAuthoringToolset.CreateGameplayEffectBlueprint",
        {"AssetPath": "/InvalidRoot/GE_ShouldFail"},
    )
    assert _is_error_response(bad_ge)

    # Missing skeleton path
    bad_anim = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.GetSkeletonAnimationInfo",
        {"AssetPath": "/Game/__DoesNotExist_UE59__/SK_Missing"},
    )
    assert _is_error_response(bad_anim)

    # Calling PIE actor function when PIE is not running
    bad_pie = mock_agent_client.call_cpp_tool(
        "AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction",
        {"ActorIdentifier": "NonExistentActor", "FunctionName": "ReceiveBeginPlay"},
    )
    assert _is_error_response(bad_pie)


def test_ue59_enhanced_input_authoring_lifecycle(mock_agent_client):
    """Exercise EnhancedInput authoring tools end-to-end via UE 5.9 /mcp."""
    uid = uuid.uuid4().hex[:8]
    folder = f"/Game/__AgentFrameworkE2E_{uid}"
    ia_path = f"{folder}/IA_E2EMove"
    imc_path = f"{folder}/IMC_E2EDefault"

    try:
        ia_resp = mock_agent_client.call_cpp_tool(
            "AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.CreateInputAction",
            {"AssetPath": ia_path, "ValueType": "Axis2D"},
        )
        ia_val = _extract_return_value(ia_resp)
        assert ia_val.get("AssetPath") == ia_path
        assert ia_val.get("ValueType") == "Axis2D"

        imc_resp = mock_agent_client.call_cpp_tool(
            "AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.CreateInputMappingContext",
            {"AssetPath": imc_path},
        )
        imc_val = _extract_return_value(imc_resp)
        assert imc_val.get("AssetPath") == imc_path

        add_resp = mock_agent_client.call_cpp_tool(
            "AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.AddInputMapping",
            {
                "MappingContextPath": imc_path,
                "InputActionPath": ia_path,
                "KeyName": "W",
                "Modifiers": [{"Type": "SwizzleAxis", "Properties": {}}],
            },
        )
        add_val = _extract_return_value(add_resp)
        assert add_val.get("TotalMappings") == 1
        assert add_val.get("ModifierCount") == 1

        cfg_resp = mock_agent_client.call_cpp_tool(
            "AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.ConfigureInputMappingModifiersTriggers",
            {
                "MappingContextPath": imc_path,
                "InputActionPath": ia_path,
                "KeyName": "W",
                "Modifiers": [],
                "Triggers": [{"Type": "Pressed", "Properties": {}}],
            },
        )
        cfg_val = _extract_return_value(cfg_resp)
        assert cfg_val.get("ModifierCount") == 0
        assert cfg_val.get("TriggerCount") == 1
    finally:
        # Best-effort cleanup of test directory via UE 5.9 EditorToolset.AssetToolset.Delete
        try:
            mock_agent_client.call_cpp_tool(
                "EditorToolset.AssetToolset.Delete",
                {"Path": folder},
            )
        except Exception:
            pass
