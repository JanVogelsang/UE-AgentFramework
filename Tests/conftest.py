import json
import os
import socket
import subprocess
import sys
import time
import pytest
from mock_client import MockAgentClient

UNREAL_PATH = None
PROJECT_PATH = os.path.expandvars(r"%USERPROFILE%\Documents\Unreal Projects\AgentFrameworkTest\AgentFrameworkTest.uproject")
PORT = int(os.environ.get("UE_MCP_PORT", "8000"))


def is_port_open(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            return True
        except (socket.timeout, ConnectionRefusedError, OSError):
            return False


@pytest.fixture(scope="session")
def unreal_process():
    """
    Checks if Unreal Editor 5.9 is running with ModelContextProtocol on port 8000.
    If not running and AUTO_LAUNCH_UNREAL=1 is not set, skips live-editor E2E tests cleanly.
    """
    if is_port_open(PORT):
        print(f"\n[INFO] Detected active Unreal Editor 5.9 MCP server on port {PORT}.")
        yield None
        return

    if os.environ.get("AUTO_LAUNCH_UNREAL", "0") != "1":
        pytest.skip(
            f"Unreal Editor 5.9 MCP server is not listening on port {PORT}. "
            "Open the project in Unreal Editor 5.9 (or set AUTO_LAUNCH_UNREAL=1) to run live E2E tests."
        )

    project_path = os.path.normpath(PROJECT_PATH)
    unreal_exe = None
    engine_association = "5.9"

    if os.path.exists(project_path):
        try:
            with open(project_path, "r", encoding="utf-8") as f:
                uproject_data = json.load(f)
                if "EngineAssociation" in uproject_data:
                    engine_association = str(uproject_data["EngineAssociation"])
        except Exception:
            pass

    fallbacks = [
        r"D:\UE_5.9\Engine\Binaries\Win64\UnrealEditor.exe",
        rf"D:\UE_{engine_association}\Engine\Binaries\Win64\UnrealEditor.exe",
        rf"C:\Program Files\Epic Games\UE_{engine_association}\Engine\Binaries\Win64\UnrealEditor.exe",
        r"C:\Program Files\Epic Games\UE_5.9\Engine\Binaries\Win64\UnrealEditor.exe",
    ]
    for p in fallbacks:
        if os.path.exists(p):
            unreal_exe = p
            break

    proc = None
    if unreal_exe and os.path.exists(project_path):
        print(f"\n[INFO] Auto-launching Unreal Editor 5.9:\n  Editor: {unreal_exe}\n  Project: {project_path}")
        proc = subprocess.Popen([unreal_exe, project_path])
    else:
        pytest.skip(f"Could not locate Unreal Editor 5.9 executable or project file at '{project_path}'.")

    start_time = time.time()
    timeout = 180.0
    opened = False
    while time.time() - start_time < timeout:
        if is_port_open(PORT):
            opened = True
            break
        time.sleep(1.0)

    if not opened:
        if proc:
            proc.terminate()
        pytest.skip(f"Unreal Editor 5.9 did not open port {PORT} within {timeout} seconds.")

    yield proc

    if proc:
        proc.terminate()
        try:
            proc.wait(timeout=10.0)
        except subprocess.TimeoutExpired:
            proc.kill()


@pytest.fixture(scope="session")
def is_live_editor():
    return is_port_open(PORT)


@pytest.fixture(scope="session")
def unreal_port_wait(unreal_process):
    if not is_port_open(PORT):
        pytest.skip(f"Port {PORT} is not open.")
    yield


@pytest.fixture(scope="session")
def python_server_process():
    """
    Spawns the Python AST MCP server process running 'python -m UnrealEngine.ExternalServer.src.main'.
    """
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    env = os.environ.copy()
    if "PYTHONPATH" in env:
        env["PYTHONPATH"] = f"{project_root};{env['PYTHONPATH']}"
    else:
        env["PYTHONPATH"] = project_root

    cmd = [sys.executable, "-u", "-m", "UnrealEngine.ExternalServer.src.main"]
    process = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=project_root,
        env=env,
        text=True,
        bufsize=1,
    )

    yield process

    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


@pytest.fixture(scope="session")
def mock_agent_client(unreal_port_wait, python_server_process):
    """
    Yields a mock agent client connected to both the UE 5.9 /mcp server and Python AST MCP server.
    """
    client = MockAgentClient(
        python_process=python_server_process,
        cpp_url=f"http://127.0.0.1:{PORT}/mcp",
    )
    try:
        yield client
    finally:
        client.close_cpp_session()
