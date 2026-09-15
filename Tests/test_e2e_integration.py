import pytest
import json
import uuid

def test_python_mcp_query_cpp_ast(mock_agent_client):
    """
    Test Python MCP tool: query_cpp_ast
    """
    response = mock_agent_client.call_python_tool(
        "query_cpp_ast",
        {"query": "class AMorphTargetActor"}
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
    """
    Test Python MCP tool: generate_compile_commands
    """
    response = mock_agent_client.call_python_tool(
        "generate_compile_commands",
        {}
    )
    assert response is not None
    assert "result" in response
    result = response["result"]
    assert "content" in result
    assert len(result["content"]) > 0
    assert "Compilation generation started in background" in result["content"][0]["text"]

def test_python_mcp_search_vector_db(mock_agent_client):
    """
    Test Python MCP tool: search_vector_db
    """
    response = mock_agent_client.call_python_tool(
        "search_vector_db",
        {"query": "character movement replication"}
    )
    assert response is not None
    assert "result" in response
    result = response["result"]
    assert "content" in result
    assert len(result["content"]) > 0
    assert "search_vector_db" in result["content"][0]["text"]

def test_cpp_mcp_get_blueprint_schema(mock_agent_client):
    """
    Test C++ HTTP tool: get_blueprint_schema
    """
    response = mock_agent_client.call_cpp_tool(
        "get_blueprint_schema",
        {"asset_path": "/Game/Blueprint/Player/BP_RoundPawn"}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    result_msg = response.get("ResultMessage", "")
    schema = json.loads(result_msg)
    for key in ["asset_name", "parent_class", "variables", "custom_events"]:
        assert key in schema

def test_cpp_mcp_inject_blueprint_nodes_t3d(mock_agent_client):
    """
    Test C++ HTTP tool: inject_blueprint_nodes_t3d
    """
    valid_t3d = (
        "Begin Object Class=/Script/BlueprintGraph.K2Node_VariableGet Name=\"K2Node_VariableGet_0\"\n"
        "   NodeGuid=GUID_Node1\n"
        "   CustomProperties Pin (PinId=LINK_1,PinName=\"Output\",Direction=\"EGPD_Output\")\n"
        "End Object"
    )
    response = mock_agent_client.call_cpp_tool(
        "inject_blueprint_nodes_t3d",
        {
            "asset_path": "/Game/Blueprint/Player/BP_RoundPawn",
            "t3d_text": valid_t3d
        }
    )
    assert response is not None
    assert response.get("bSuccess") is True
    assert "Injected" in response.get("ResultMessage", "")

def test_cpp_mcp_get_cpp_reflection_info(mock_agent_client):
    """
    Test C++ HTTP tool: get_cpp_reflection_info
    """
    response = mock_agent_client.call_cpp_tool(
        "get_cpp_reflection_info",
        {"class_name": "Actor"}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    result_msg = response.get("ResultMessage", "")
    info = json.loads(result_msg)
    for key in ["class_name", "parent_class", "properties", "functions"]:
        assert key in info


@pytest.fixture(scope="module")
def pie_session(mock_agent_client):
    import time
    # Start PIE session
    res = mock_agent_client.call_cpp_tool("start_pie_session", {})
    assert res is not None
    assert res.get("bSuccess") is True
    # Wait for the session and viewport to initialize
    start_time = time.time()
    pie_ok = False
    while time.time() - start_time < 15.0:
        res_state = mock_agent_client.call_cpp_tool("query_world_state", {"classes": ["Actor"]})
        if res_state and res_state.get("bSuccess") is True:
            pie_ok = True
            break
        time.sleep(0.5)
    assert pie_ok, "PIE session failed to initialize and report world state within timeout."
    yield
    # Stop PIE session
    mock_agent_client.call_cpp_tool("stop_pie_session", {})
    time.sleep(1.0)


def test_cpp_mcp_extract_ui_state(mock_agent_client, pie_session):
    """
    Test C++ HTTP tool: extract_ui_state
    """
    response = mock_agent_client.call_cpp_tool(
        "extract_ui_state",
        {}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    result_msg = response.get("ResultMessage", "")
    ui_state = json.loads(result_msg)
    assert "umg" in ui_state
    assert "slate" in ui_state


def test_cpp_mcp_trigger_ui_element(mock_agent_client, pie_session):
    """
    Test C++ HTTP tool: trigger_ui_element
    """
    # First extract current UI layout to find a valid widget
    response = mock_agent_client.call_cpp_tool("extract_ui_state", {})
    assert response is not None
    assert response.get("bSuccess") is True
    ui_state = json.loads(response.get("ResultMessage", ""))
    
    widget_path = None
    if ui_state.get("umg"):
        widget_path = list(ui_state["umg"].keys())[0]
    elif ui_state.get("slate"):
        widget_path = list(ui_state["slate"].keys())[0]

    # If any interactable widget is active in viewport, trigger programmatic click on it
    if widget_path:
        response = mock_agent_client.call_cpp_tool(
            "trigger_ui_element",
            {"widget_path": widget_path}
        )
        assert response is not None
        assert response.get("bSuccess") is True
        assert "clicked widget" in response.get("ResultMessage", "").lower()


def test_cpp_mcp_query_world_state(mock_agent_client, pie_session):
    """
    Test C++ HTTP tool: query_world_state
    """
    # Query with generic 'Actor' class filter which always matches loaded level actors
    response = mock_agent_client.call_cpp_tool(
        "query_world_state",
        {"classes": ["Actor"]}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    result_msg = response.get("ResultMessage", "")
    world_state = json.loads(result_msg)
    assert "player" in world_state
    assert "actors" in world_state


def test_cpp_mcp_execute_python_script_validation(mock_agent_client):
    """
    Test that execute_python_script fails validation under various invalid justifications
    and succeeds under a valid justification.
    """
    # 1. Missing justification
    response = mock_agent_client.call_cpp_tool(
        "execute_python_script",
        {
            "script": "print('hello')"
        }
    )
    assert response is not None
    assert response.get("bSuccess") is False
    assert "is required" in "".join(response.get("Errors", []))

    # 2. Justification too short (< 10 chars)
    response = mock_agent_client.call_cpp_tool(
        "execute_python_script",
        {
            "script": "print('hello')",
            "justification_why_native_tools_or_skills_are_insufficient": "too short"
        }
    )
    assert response is not None
    assert response.get("bSuccess") is False
    assert "too short" in "".join(response.get("Errors", []))

    # 3. Justification too long (> 1000 chars)
    long_justification = "a" * 1001
    response = mock_agent_client.call_cpp_tool(
        "execute_python_script",
        {
            "script": "print('hello')",
            "justification_why_native_tools_or_skills_are_insufficient": long_justification
        }
    )
    assert response is not None
    assert response.get("bSuccess") is False
    assert "too long" in "".join(response.get("Errors", []))

    # 4. Justification too few words (< 4 words)
    response = mock_agent_client.call_cpp_tool(
        "execute_python_script",
        {
            "script": "print('hello')",
            "justification_why_native_tools_or_skills_are_insufficient": "No native tools"
        }
    )
    assert response is not None
    assert response.get("bSuccess") is False
    assert "too vague" in "".join(response.get("Errors", []))

    # 5. Low-effort phrase check
    response = mock_agent_client.call_cpp_tool(
        "execute_python_script",
        {
            "script": "print('hello')",
            "justification_why_native_tools_or_skills_are_insufficient": "Testing python execution now"
        }
    )
    assert response is not None
    assert response.get("bSuccess") is False
    assert "flagged as a low-effort placeholder" in "".join(response.get("Errors", []))

    # 6. Valid justification
    response = mock_agent_client.call_cpp_tool(
        "execute_python_script",
        {
            "script": "print('hello')",
            "justification_why_native_tools_or_skills_are_insufficient": "We need to run custom Python reflection because no native tool can read metadata of non-blueprint UObjects"
        }
    )
    assert response is not None
    errors_str = "".join(response.get("Errors", []))
    assert "justification_why_native_tools_or_skills_are_insufficient" not in errors_str
    assert "too short" not in errors_str
    assert "too long" not in errors_str
    assert "too vague" not in errors_str
    assert "placeholder" not in errors_str

def test_cpp_mcp_query_epic_assistant(mock_agent_client):
    """
    Test C++ HTTP tool: query_epic_assistant
    """
    response = mock_agent_client.call_cpp_tool(
        "query_epic_assistant",
        {"prompt": "Hello AI Assistant"}
    )
    assert response is not None
    if response.get("bSuccess") is True:
        assert len(response.get("ResultMessage", "")) > 0
    else:
        errors = "".join(response.get("Errors", []))
        assert "not enabled or loaded" in errors or "Failed to locate" in errors

def test_cpp_mcp_search_assets(mock_agent_client):
    """
    Test C++ HTTP tool: search_assets
    """
    response = mock_agent_client.call_cpp_tool(
        "search_assets",
        {"query": "BP_RoundPawn", "max_results": 5}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    assert "BP_RoundPawn" in response.get("ResultMessage", "")

def test_cpp_mcp_list_directory(mock_agent_client):
    """
    Test C++ HTTP tool: list_directory
    """
    response = mock_agent_client.call_cpp_tool(
        "list_directory",
        {"directory": "Source"}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    assert "Contents of directory:" in response.get("ResultMessage", "")

def test_cpp_mcp_read_file_snippet(mock_agent_client):
    """
    Test C++ HTTP tool: read_file_snippet
    """
    response = mock_agent_client.call_cpp_tool(
        "read_file_snippet",
        {"file_path": "Config/DefaultEngine.ini", "start_line": 1, "end_line": 10}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    assert "File:" in response.get("ResultMessage", "")

def test_cpp_mcp_get_tool_info(mock_agent_client):
    """
    Test C++ HTTP tool: get_tool_info
    """
    response = mock_agent_client.call_cpp_tool(
        "get_tool_info",
        {"tool_name": "get_tool_info"}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    result_data = json.loads(response.get("ResultMessage", "{}"))
    assert "tool" in result_data
    assert result_data["tool"].get("name") == "get_tool_info"

def test_cpp_mcp_list_tools_in_category(mock_agent_client):
    """
    Test C++ HTTP tool: list_tools_in_category
    """
    response = mock_agent_client.call_cpp_tool(
        "list_tools_in_category",
        {"category": "Context"}
    )
    assert response is not None
    assert response.get("bSuccess") is True
    result_data = json.loads(response.get("ResultMessage", "{}"))
    assert "tools" in result_data
    tool_names = [t.get("name") for t in result_data["tools"]]
    assert "search_assets" in tool_names


def test_cpp_mcp_data_asset_actions(mock_agent_client):
    """
    Test C++ HTTP tools: create_data_asset, set_data_asset_properties, and get_data_asset_info
    """
    # 1. Create a PrimaryDataAsset
    asset_path = f"/Game/DA_TestAsset_{uuid.uuid4().hex[:8]}"
    create_response = mock_agent_client.call_cpp_tool(
        "create_data_asset",
        {
            "asset_path": asset_path,
            "class_name": "PrimaryDataAsset"
        }
    )
    assert create_response is not None
    assert create_response.get("bSuccess") is True
    assert "Successfully created Data Asset" in create_response.get("ResultMessage", "")

    # 2. Get Data Asset Info
    info_response = mock_agent_client.call_cpp_tool(
        "get_data_asset_info",
        {
            "asset_path": asset_path
        }
    )
    assert info_response is not None
    assert info_response.get("bSuccess") is True
    info_msg = info_response.get("ResultMessage", "")
    info_json = json.loads(info_msg)
    assert info_json.get("asset_path") == asset_path
    assert info_json.get("class_name") == "PrimaryDataAsset"

    # 3. Set Properties on the Data Asset
    set_response = mock_agent_client.call_cpp_tool(
        "set_data_asset_properties",
        {
            "asset_path": asset_path,
            "properties": {}
        }
    )
    assert set_response is not None
    assert set_response.get("bSuccess") is True
    assert "Successfully updated" in set_response.get("ResultMessage", "")


def test_cpp_mcp_data_table_actions(mock_agent_client):
    """
    Test C++ HTTP tools: create_data_table and import_json_to_datatable
    """
    asset_path = f"/Game/TestDataTable_{uuid.uuid4().hex[:8]}"

    # 1. Create DataTable
    create_response = mock_agent_client.call_cpp_tool(
        "create_data_table",
        {
            "asset_path": asset_path,
            "row_struct": "GameplayTagTableRow"
        }
    )
    assert create_response is not None
    assert create_response.get("bSuccess") is True
    assert "Created DataTable" in create_response.get("ResultMessage", "")

    # 2. Import JSON into DataTable
    import_response = mock_agent_client.call_cpp_tool(
        "import_json_to_datatable",
        {
            "asset_path": asset_path,
            "json_data": '[{"Name": "TestTag1", "Tag": "TestTag1", "Comment": "Test comment 1"}]'
        }
    )
    assert import_response is not None
    assert import_response.get("bSuccess") is True
    assert "Successfully imported" in import_response.get("ResultMessage", "")


def test_cpp_mcp_niagara_data_channel_and_effect_type(mock_agent_client):
    """
    Test C++ HTTP tools: create_niagara_data_channel and create_niagara_effect_type
    """
    ndc_path = f"/Game/VFX/NDC_Test_{uuid.uuid4().hex[:8]}"
    ndc_response = mock_agent_client.call_cpp_tool(
        "create_niagara_data_channel",
        {
            "asset_path": ndc_path
        }
    )
    assert ndc_response is not None
    error_str = " ".join(ndc_response.get("Errors", []))
    if not ndc_response.get("bSuccess") and ("Unknown Niagara tool" in error_str or "No executor registered" in error_str):
        pytest.skip("Editor session is running older plugin binaries prior to C++ rebuild.")

    assert ndc_response.get("bSuccess") is True
    assert "Created Niagara Data Channel" in ndc_response.get("ResultMessage", "")

    effect_type_path = f"/Game/VFX/NE_Test_{uuid.uuid4().hex[:8]}"
    effect_type_response = mock_agent_client.call_cpp_tool(
        "create_niagara_effect_type",
        {
            "asset_path": effect_type_path
        }
    )
    assert effect_type_response is not None
    assert effect_type_response.get("bSuccess") is True
    assert "Created Niagara Effect Type" in effect_type_response.get("ResultMessage", "")


def test_cpp_mcp_universal_create_asset(mock_agent_client):
    """
    Test C++ HTTP tool: create_asset across multiple Unreal Engine asset types
    (PhysicalMaterial, CurveFloat, InputMappingContext, NiagaraDataChannelAsset)
    """
    # 1. Test Physical Material creation
    pm_path = f"/Game/Physics/PM_Test_{uuid.uuid4().hex[:8]}"
    pm_response = mock_agent_client.call_cpp_tool(
        "create_asset",
        {
            "asset_path": pm_path,
            "asset_class": "PhysicalMaterial"
        }
    )
    assert pm_response is not None
    error_str = " ".join(pm_response.get("Errors", []))
    if not pm_response.get("bSuccess") and ("Unknown" in error_str or "No executor registered" in error_str or "Could not determine context action" in error_str):
        pytest.skip("Editor session is running older plugin binaries prior to C++ rebuild.")

    assert pm_response.get("bSuccess") is True
    assert "Successfully created asset" in pm_response.get("ResultMessage", "")

    # 2. Test CurveFloat creation
    curve_path = f"/Game/Curves/Curve_Test_{uuid.uuid4().hex[:8]}"
    curve_response = mock_agent_client.call_cpp_tool(
        "create_asset",
        {
            "asset_path": curve_path,
            "asset_class": "CurveFloat"
        }
    )
    assert curve_response is not None
    assert curve_response.get("bSuccess") is True
    assert "Successfully created asset" in curve_response.get("ResultMessage", "")

    # 3. Test InputMappingContext creation
    imc_path = f"/Game/Input/IMC_Test_{uuid.uuid4().hex[:8]}"
    imc_response = mock_agent_client.call_cpp_tool(
        "create_asset",
        {
            "asset_path": imc_path,
            "asset_class": "InputMappingContext"
        }
    )
    assert imc_response is not None
    assert imc_response.get("bSuccess") is True
    assert "Successfully created asset" in imc_response.get("ResultMessage", "")


def test_cpp_mcp_niagara_pin_di_and_reset(mock_agent_client):
    """
    Test C++ HTTP tools: set_niagara_module_pin (direct DI assignment), reset_niagara_module_pin, and list_niagara_parameters
    """
    system_path = f"/Game/VFX/NS_Test_{uuid.uuid4().hex[:8]}"
    create_resp = mock_agent_client.call_cpp_tool(
        "create_niagara_system",
        {"asset_path": system_path}
    )
    assert create_resp is not None
    error_str = " ".join(create_resp.get("Errors", []))
    if not create_resp.get("bSuccess") and ("Unknown" in error_str or "No executor" in error_str or "Could not connect" in error_str):
        pytest.skip("Editor session is running older plugin binaries prior to C++ rebuild.")

    # 1. Test list_niagara_parameters across scopes
    list_resp = mock_agent_client.call_cpp_tool(
        "list_niagara_parameters",
        {
            "system_path": system_path,
            "scope": "all",
            "include_module_inputs": True,
            "include_orphaned_nodes": True
        }
    )
    assert list_resp is not None
    assert list_resp.get("bSuccess") is True

    # 2. Test reset_niagara_module_pin endpoint routing and validation
    reset_resp = mock_agent_client.call_cpp_tool(
        "reset_niagara_module_pin",
        {
            "system_path": system_path,
            "phase": "SystemSpawn",
            "module_type": "NonExistentModule",
            "pin_name": "TestPin"
        }
    )
    assert reset_resp is not None
    reset_errors = " ".join(reset_resp.get("Errors", []))
    assert "not found in phase" in reset_errors or reset_resp.get("bSuccess") is True

    # 3. Test set_niagara_module_pin accepts asset_path without value
    pin_resp = mock_agent_client.call_cpp_tool(
        "set_niagara_module_pin",
        {
            "system_path": system_path,
            "phase": "SystemSpawn",
            "module_type": "NonExistentModule",
            "pin_name": "Data",
            "asset_path": "/Game/VFX/NDC_Test"
        }
    )
    assert pin_resp is not None
    pin_errors = " ".join(pin_resp.get("Errors", []))
    # It must NOT fail with "Either 'value' or 'link_parameter' must be provided"
    assert "Either 'value' or 'link_parameter' must be provided" not in pin_errors
    assert "not found in phase" in pin_errors or pin_resp.get("bSuccess") is True


def test_cpp_mcp_niagara_curve_module_input(mock_agent_client):
    """Test set_niagara_module_pin accepts curve_keys without value/asset_path,
    and validates proper curve_keys routing."""
    system_path = "/Game/TestSystem_Curves"
    resp = mock_agent_client.call_cpp_tool(
        "set_niagara_module_pin",
        {
            "system_path": system_path,
            "phase": "SystemSpawn",
            "module_type": "ScaleSpriteSize",
            "pin_name": "Uniform Curve Sprite Scale",
            "curve_keys": [
                {"time": 0.0, "value": 1.0},
                {"time": 1.0, "value": 0.0}
            ]
        }
    )
    assert resp is not None
    errors = " ".join(resp.get("Errors", []))
    assert "must be provided for set_niagara_module_pin" not in errors

    # Test multi-channel curve_keys with channel aliases (R, G, B)
    resp_color = mock_agent_client.call_cpp_tool(
        "set_niagara_module_pin",
        {
            "system_path": system_path,
            "phase": "ParticleUpdate",
            "emitter_name": "TestEmitter",
            "module_type": "ScaleColor",
            "pin_name": "Linear Color Curve",
            "curve_keys": {
                "R": [{"time": 0.0, "value": 1.0}, {"time": 1.0, "value": 0.2}],
                "G": [{"time": 0.0, "value": 0.8}, {"time": 1.0, "value": 0.0}],
                "B": [{"time": 0.0, "value": 0.1}, {"time": 1.0, "value": 0.0}]
            }
        }
    )
    assert resp_color is not None
    color_errors = " ".join(resp_color.get("Errors", []))
    assert "must be provided for set_niagara_module_pin" not in color_errors








