import os
import sys
import subprocess
import time
import socket
import pytest
import http.server
import threading
import json
from mock_client import MockAgentClient

UNREAL_PATH = None
PROJECT_PATH = os.path.expandvars(r"%USERPROFILE%\Documents\Unreal Projects\AgentFrameworkTest\AgentFrameworkTest.uproject")
PORT = 18777

def is_port_open(port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            return True
        except (socket.timeout, ConnectionRefusedError):
            return False

@pytest.fixture(scope="session")
def unreal_process():
    """
    Ensures that the Unreal Editor is running. If not, auto-launches it or prompts the user.
    """
    if is_port_open(PORT):
        print(f"\n[INFO] Detecting active Unreal Editor on port {PORT}. Proceeding.")
        yield None
        return

    # Auto-launch Editor
    project_path = os.path.normpath(PROJECT_PATH)
    
    unreal_exe = None
    engine_association = "5.8"  # Default fallback version
    
    # Parse EngineAssociation from .uproject JSON
    if os.path.exists(project_path):
        try:
            with open(project_path, "r") as f:
                uproject_data = json.load(f)
                if "EngineAssociation" in uproject_data:
                    engine_association = str(uproject_data["EngineAssociation"])
        except Exception:
            pass

    try:
        import winreg
        installed_dir = None
        # Check standard launcher registry path first
        if "." in engine_association:
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\EpicGames\Unreal Engine\{engine_association}") as key:
                    installed_dir, _ = winreg.QueryValueEx(key, "InstalledDirectory")
            except Exception:
                pass
            
            if installed_dir:
                for name in ["UnrealEditor.exe", "UnrealEditor-Cmd.exe"]:
                    p = os.path.join(installed_dir, "Engine", "Binaries", "Win64", name)
                    if os.path.exists(p):
                        unreal_exe = p
                        break
        
        # Check custom builds registry path if launcher query failed
        if not unreal_exe:
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Epic Games\Unreal Engine\Builds") as key:
                    installed_dir, _ = winreg.QueryValueEx(key, engine_association)
            except Exception:
                pass

            if installed_dir:
                for name in ["UnrealEditor.exe", "UnrealEditor-Cmd.exe"]:
                    p = os.path.join(installed_dir, "Engine", "Binaries", "Win64", name)
                    if os.path.exists(p):
                        unreal_exe = p
                        break
    except Exception:
        pass

    if not unreal_exe:
        fallbacks = [
            rf"C:\Program Files\Epic Games\UE_{engine_association}\Engine\Binaries\Win64\UnrealEditor.exe",
            rf"C:\Program Files\Epic Games\UE_{engine_association}\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
            r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
            r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
            r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe",
            r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
        ]
        for p in fallbacks:
            if os.path.exists(p):
                unreal_exe = p
                break

    proc = None
    if unreal_exe and os.path.exists(project_path):
        print(f"\n[INFO] Unreal Editor is closed. Auto-launching:\n  Editor: {unreal_exe}\n  Project: {project_path}")
        proc = subprocess.Popen([unreal_exe, project_path])
    else:
        print(f"\n[WARNING] Could not locate Unreal Editor executable or project file at '{project_path}'.")

    print(f"\n[IMPORTANT] Waiting for Unreal Editor to start on port {PORT}...")
    print("If it does not start automatically, please open your editor project manually.")

    start_time = time.time()
    timeout = 180.0
    opened = False
    while time.time() - start_time < timeout:
        if is_port_open(PORT):
            opened = True
            break
        time.sleep(1.0)

    if not opened:
        pytest.exit(f"\n[ERROR] Unreal Editor did not start or register on port {PORT} within {timeout} seconds.\n"
                    f"Please open the editor manually and verify the AgentFramework plugin is loaded.")

    yield proc

    if proc:
        print("\n[INFO] Shutting down spawned Unreal Editor instance...")
        proc.terminate()
        try:
            proc.wait(timeout=10.0)
        except subprocess.TimeoutExpired:
            proc.kill()

@pytest.fixture(scope="session")
def is_live_editor(unreal_process):
    return True

@pytest.fixture(scope="session")
def unreal_port_wait(unreal_process):
    """
    Polls port 18777 with a 120-second timeout until it is open and responds to a readiness probe.
    """
    import urllib.request
    import json
    start_time = time.time()
    timeout = 120.0
    success = False

    print(f"\nPolling port {PORT} with readiness probe (timeout: {timeout}s)...")
    while time.time() - start_time < timeout:
        if is_port_open(PORT):
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/api/tools", timeout=2.0) as response:
                    if response.status == 200:
                        data = json.loads(response.read().decode('utf-8'))
                        if isinstance(data, list) or (isinstance(data, dict) and isinstance(data.get("tools"), list)):
                            success = True
                            break
            except Exception as e:
                # Readiness probe request failed, retry
                pass
        time.sleep(0.5)

    if not success:
        pytest.exit(f"Readiness probe on port {PORT} failed or did not return a valid list within {timeout} seconds.")

    print(f"Port {PORT} readiness probe succeeded. Proceeding to run tests.")
    yield

@pytest.fixture(scope="session")
def python_server_process():
    """
    Spawns the Python server process as a subprocess running 'python -m ExternalServer.src.main'.
    """
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    
    env = os.environ.copy()
    if "PYTHONPATH" in env:
        env["PYTHONPATH"] = f"{project_root};{env['PYTHONPATH']}"
    else:
        env["PYTHONPATH"] = project_root

    cmd = [sys.executable, "-u", "-m", "UnrealEngine.ExternalServer.src.main"]
    print(f"\nSpawning Python MCP server process with command: {' '.join(cmd)}")
    
    process = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=project_root,
        env=env,
        text=True,
        bufsize=1
    )

    yield process

    # Teardown
    if process.poll() is None:
        print("\nTerminating Python MCP server process...")
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()

@pytest.fixture(scope="session")
def mock_agent_client(unreal_port_wait, python_server_process):
    """
    Yields a mock agent client fixture connected to both the C++ and Python MCP servers.
    """
    client = MockAgentClient(
        python_process=python_server_process,
        cpp_url=f"http://127.0.0.1:{PORT}",
        endpoint_file=os.path.join(os.path.dirname(os.path.normpath(PROJECT_PATH)), "Saved", "AgentFramework", "endpoint.json")
    )
    yield client

@pytest.fixture(scope="session", autouse=True)
def setup_test_blueprint(mock_agent_client):
    """
    Ensures that the required test Blueprint /Game/Blueprint/Player/BP_RoundPawn
    and test Input assets (/Game/Input/IA_TestChallenger, /Game/Input/IMC_TestChallenger)
    exist in the active editor workspace.
    """
    if is_port_open(PORT):
        print("\n[INFO] Setting up BP_RoundPawn in the live editor...")
        try:
            res = mock_agent_client.call_cpp_tool(
                "get_blueprint_info",
                {"asset_path": "/Game/Blueprint/Player/BP_RoundPawn"}
            )
            if res.get("bSuccess") is True:
                print("[INFO] BP_RoundPawn already exists.")
            else:
                res = mock_agent_client.call_cpp_tool(
                    "create_blueprint_actor",
                    {
                        "asset_path": "/Game/Blueprint/Player/BP_RoundPawn",
                        "parent_class": "Pawn",
                        "variables": [
                            {"name": "Health", "type": "float"},
                            {"name": "Speed", "type": "float"},
                            {"name": "bIsActive", "type": "bool"}
                        ]
                    }
                )
                if res.get("bSuccess") is True:
                    print("[INFO] Successfully created BP_RoundPawn.")
                else:
                    print(f"[WARNING] Failed to create BP_RoundPawn: {res.get('Errors')}")
        except Exception as e:
            print(f"[WARNING] Exception creating BP_RoundPawn: {e}")

        print("\n[INFO] Setting up input test assets in the live editor...")
        try:
            mock_agent_client.call_cpp_tool(
                "create_input_action",
                {
                    "asset_path": "/Game/Input/IA_TestChallenger",
                    "value_type": "Axis2D"
                }
            )
            mock_agent_client.call_cpp_tool(
                "create_input_mapping_context",
                {
                    "asset_path": "/Game/Input/IMC_TestChallenger"
                }
            )
            mock_agent_client.call_cpp_tool(
                "add_input_mapping",
                {
                    "ContextAsset": "/Game/Input/IMC_TestChallenger",
                    "InputActionAsset": "/Game/Input/IA_TestChallenger",
                    "Key": "W"
                }
            )
        except Exception as e:
            print(f"[WARNING] Exception creating input test assets: {e}")
