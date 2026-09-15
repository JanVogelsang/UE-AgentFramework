# AgentFramework E2E Integration Tests

This directory contains the end-to-end integration tests for the AgentFramework Dual-MCP architecture.

## Overview

The test framework connects:
1. **Internal C++ MCP Server**: Running inside Unreal Engine Editor (spawning `UnrealEditor-Cmd.exe` or reusing a running instance). It communicates via HTTP POST requests on port `18777` (default, configurable, or auto-discovered via endpoint file).
2. **External Indexing & RAG MCP Server (Python)**: Running as a standalone process communicating via JSON-RPC 2.0 over standard input/output.

## Directory Structure

- `requirements.txt`: Python package requirements for running the tests.
- `conftest.py`: pytest fixtures that manage process lifecycles (Unreal Editor and Python MCP server).
- `mock_client.py`: The `MockAgentClient` that wraps the JSON-RPC interface for the Python server and the HTTP client for the C++ server.
- `test_e2e_integration.py`: Integration tests verifying the connection and tool invocation flow for both servers.

## Environment Setup & Test Execution (Windows)

On Windows systems, PATH resolution can sometimes point to restricted shell Pythons (e.g. MinGW/MSYS2) missing required packages.

### Recommended Test Runner
Always use the PowerShell runner wrapper from the root of the project to ensure the correct Python environment and DLL directories are loaded:
```powershell
powershell -File .\run_tests.ps1
```

Or run directly using the standard user Python binary:
```powershell
& "$env:USERPROFILE\AppData\Local\Microsoft\WindowsApps\python.exe" -m pip install -r Tests/requirements.txt --user
powershell -File .\run_tests.ps1
```

---

## Benchmarking & Baseline Evaluation

This directory maintains the 42-task Unreal Engine benchmark baseline telemetry dataset.

### Baseline Data & Comparison

- **`benchmark_baseline.json`**: Authoritative post-optimization 42-task baseline dataset.
- **`BENCHMARK_BASELINE.md`**: Human-readable baseline telemetry comparison report.
- **`compare_benchmarks.py`**: Automated comparison tool to evaluate new benchmark runs against the baseline.

#### How to Compare Benchmark Runs
To run a comparison of new benchmark JSON results against the baseline:
```powershell
python Tests/compare_benchmarks.py --current path/to/new_run_results.json
```
