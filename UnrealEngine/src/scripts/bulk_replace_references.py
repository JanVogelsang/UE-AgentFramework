import json
import urllib.request
import urllib.error

from editor_endpoint import editor_port, execute_tool_url, json_headers

EDITOR_HTTP_URL = execute_tool_url()

def bulk_replace_references(source_path, target_path):
    """
    Consolidates assets by replacing all references to source_path with target_path,
    then deletes source_path via the native C++ MCP tool 'consolidate_asset_references'.
    """
    if not source_path or not target_path:
        print("Error: Both source_path and target_path must be specified.")
        return False

    if source_path == target_path:
        print("Warning: Source and target paths are identical. Skipping.")
        return True

    payload = {
        "tool_name": "consolidate_asset_references",
        "parameters": {
            "source_asset_path": source_path,
            "target_asset_path": target_path
        }
    }

    req = urllib.request.Request(
        EDITOR_HTTP_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers=json_headers()
    )

    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            res_data = json.loads(response.read().decode("utf-8"))
            if res_data.get("bSuccess"):
                print(f"Success: {res_data.get('ResultMessage')}")
                return True
            else:
                print(f"Error: {res_data.get('Errors')}")
                return False
    except urllib.error.URLError as e:
        print(f"Failed to connect to Editor HTTP server on port {editor_port()}: {e}")
        return False

if __name__ == "__main__":
    # Example usage:
    # bulk_replace_references("/Game/OldMat", "/Game/NewMat")
    pass
