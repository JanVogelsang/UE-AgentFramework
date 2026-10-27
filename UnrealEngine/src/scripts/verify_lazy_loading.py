#!/usr/bin/env python3
# Copyright 2026 AgentFramework. All Rights Reserved.

import os
import json
import urllib.request
import urllib.error
import sys

from editor_endpoint import execute_tool_url, json_headers, tools_url

def main():
    # 1. Determine target project directory
    user_profile = os.environ.get("USERPROFILE") or os.path.expanduser("~")
    tau_game_dir = os.path.join(user_profile, "Documents", "Unreal Projects", "AgentFrameworkTest")
    if "TARGET_PROJECT_DIR" in os.environ:
        tau_game_dir = os.environ["TARGET_PROJECT_DIR"]

    active_skills_paths = [
        os.path.join(tau_game_dir, ".agents", "active_skills.json"),
        os.path.join(tau_game_dir, "active_skills.json")
    ]

    print(f"Target game project: {tau_game_dir}")

    # a) Clear/remove active_skills.json (or set to empty)
    for p in active_skills_paths:
        if os.path.exists(p):
            try:
                os.remove(p)
                print(f"Cleared/removed active skills file: {p}")
            except Exception as e:
                print(f"Error removing {p}: {e}")

    url_tools = tools_url()
    url_execute = execute_tool_url()

    # b) Query /api/tools from the running C++ editor server
    print(f"Querying tools from {url_tools} (Base State)...")
    try:
        req = urllib.request.Request(url_tools, method="GET")
        with urllib.request.urlopen(req, timeout=5) as response:
            base_data = json.loads(response.read().decode("utf-8"))
    except Exception as e:
        print(f"Error connecting to C++ server at {url_tools}: {e}")
        print("Please make sure the Unreal Editor with the AgentFramework plugin is running.")
        sys.exit(1)

    def count_tools(data):
        total = 0
        for domain in data.get("tools", []):
            total += len(domain.get("tools", []))
        return total

    base_tool_count = count_tools(base_data)
    base_raw_len = len(json.dumps(base_data))
    base_token_est = base_raw_len // 4
    print(f"Base State: {base_tool_count} tools, estimated {base_token_est} tokens.")

    # c) Call /api/execute_tool to invoke activate_skill with skill_name = "animation"
    print("Invoking activate_skill for 'animation'...")
    payload = {
        "name": "activate_skill",
        "arguments": {
            "skill_name": "animation"
        }
    }
    req_body = json.dumps(payload).encode("utf-8")
    try:
        req = urllib.request.Request(
            url_execute,
            data=req_body,
            headers=json_headers(),
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=5) as response:
            exec_result = json.loads(response.read().decode("utf-8"))
            print(f"Activation result: {exec_result}")
            if not exec_result.get("bSuccess", False):
                print(f"Failed to execute activate_skill: {exec_result.get('Errors')}")
                sys.exit(1)
    except Exception as e:
        print(f"Error executing tool activate_skill: {e}")
        sys.exit(1)

    # d) Query /api/tools again, verifying that animation tools are now present
    print("Querying tools again (Active Skill State)...")
    try:
        req = urllib.request.Request(url_tools, method="GET")
        with urllib.request.urlopen(req, timeout=5) as response:
            active_data = json.loads(response.read().decode("utf-8"))
    except Exception as e:
        print(f"Error querying active tools: {e}")
        sys.exit(1)

    active_tool_count = count_tools(active_data)
    active_raw_len = len(json.dumps(active_data))
    active_token_est = active_raw_len // 4

    # Verify animation tools are present
    has_animation_tools = False
    for domain in active_data.get("tools", []):
        if domain.get("domain") == "animation_tools":
            has_animation_tools = True
            break

    print(f"Active State: {active_tool_count} tools, estimated {active_token_est} tokens.")
    print(f"Animation tools domain present: {has_animation_tools}")

    # e) Calculate and output token difference
    token_diff = active_token_est - base_token_est
    token_reduction_pct = (token_diff / active_token_est) * 100 if active_token_est > 0 else 0
    print("\n=== VERIFICATION RESULTS ===")
    print(f"Base (Lazy-Loaded) Tool Count: {base_tool_count}")
    print(f"Active (Animation Active) Tool Count: {active_tool_count}")
    print(f"Base Token Count Estimate: {base_token_est}")
    print(f"Active Token Count Estimate: {active_token_est}")
    print(f"Token Count Difference: {token_diff}")
    print(f"Token Reduction (Base vs. Active): {token_reduction_pct:.2f}%")

    if not has_animation_tools:
        print("ERROR: Animation tools were not successfully lazy-loaded.")
        sys.exit(1)

    print("SUCCESS: Lazy-loading verification completed successfully.")

if __name__ == "__main__":
    main()
