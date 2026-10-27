import json
import urllib.request
import urllib.error

from editor_endpoint import editor_port, execute_tool_url, json_headers

EDITOR_HTTP_URL = execute_tool_url()

def clean_naming_conventions(folder_path, recursive=True, dry_run=False):
    """
    Scans folder_path and enforces UE5 asset naming conventions using the native C++ action tool.
    """
    payload = {
        "tool_name": "enforce_naming_conventions",
        "parameters": {
            "folder_path": folder_path,
            "recursive": recursive,
            "dry_run": dry_run
        }
    }

    req = urllib.request.Request(
        EDITOR_HTTP_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers=json_headers()
    )

    try:
        with urllib.request.urlopen(req, timeout=60) as response:
            res_data = json.loads(response.read().decode("utf-8"))
            if res_data.get("bSuccess"):
                renamed_count = res_data.get("renamed_assets_count", 0)
                print(f"Success: Enforced naming conventions on {folder_path}. Renamed: {renamed_count}")
                return True
            else:
                print(f"Error: {res_data.get('Errors')}")
                return False
    except urllib.error.URLError as e:
        print(f"Failed to connect to Editor HTTP server on port {editor_port()}: {e}")
        return False

if __name__ == "__main__":
    # Example usage:
    # clean_naming_conventions("/Game/TestFolder")
    pass
