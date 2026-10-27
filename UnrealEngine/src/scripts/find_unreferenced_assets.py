import json
import urllib.request
import urllib.error

from editor_endpoint import editor_port, execute_tool_url, json_headers

EDITOR_HTTP_URL = execute_tool_url()

def find_unreferenced_assets(folder_path, include_soft_references=True):
    """
    Scans folder_path for unreferenced assets via native C++ AssetRegistry queries.
    """
    payload = {
        "tool_name": "find_unreferenced_assets",
        "parameters": {
            "folder_path": folder_path,
            "include_soft_references": include_soft_references
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
                unreferenced = res_data.get("unreferenced_assets", [])
                print(f"Found {len(unreferenced)} unreferenced assets in {folder_path}:")
                for asset in unreferenced:
                    print(f"  - {asset}")
                return unreferenced
            else:
                print(f"Error: {res_data.get('Errors')}")
                return []
    except urllib.error.URLError as e:
        print(f"Failed to connect to Editor HTTP server on port {editor_port()}: {e}")
        return []

if __name__ == "__main__":
    # Example usage:
    # find_unreferenced_assets("/Game/TestFolder")
    pass
