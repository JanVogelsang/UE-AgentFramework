import json
import urllib.request
import urllib.error

from editor_endpoint import editor_port, execute_tool_url, json_headers

EDITOR_HTTP_URL = execute_tool_url()

def organize_assets_by_type(folder_path, recursive=True):
    """
    Organizes assets in folder_path recursively into type-specific subfolders using native C++ action tool.
    """
    payload = {
        "tool_name": "organize_assets_by_type",
        "parameters": {
            "folder_path": folder_path,
            "recursive": recursive
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
                moved_count = res_data.get("moved_assets_count", 0)
                print(f"Success: Organized {folder_path}. Moved {moved_count} assets.")
                return True
            else:
                print(f"Error: {res_data.get('Errors')}")
                return False
    except urllib.error.URLError as e:
        print(f"Failed to connect to Editor HTTP server on port {editor_port()}: {e}")
        return False

if __name__ == "__main__":
    # Example usage:
    # organize_assets_by_type("/Game/TestFolder")
    pass
