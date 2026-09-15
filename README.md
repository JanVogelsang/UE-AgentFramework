<p align="center">
  <h1 align="center">UE-AgentFramework</h1>
  <p align="center">
    <strong>Open-source AI agent integration for Unreal Engine.</strong>
  </p>
  <p align="center">
    Connect the coding agent you already use — Antigravity, Claude Code, OpenAI Codex, Kilo Code, or any MCP client — to a live Unreal Editor. Free, MIT-licensed, and everything stays on your machine.
  </p>
</p>

<p align="center">
  <img alt="License: MIT" src="https://img.shields.io/badge/License-MIT-green.svg">
  <img alt="Unreal Engine 5.7 | 5.8" src="https://img.shields.io/badge/Unreal%20Engine-5.7%20%7C%205.8-blue.svg">
  <img alt="Bring your own agent" src="https://img.shields.io/badge/Agent-Bring%20your%20own-orange.svg">
  <img alt="PRs welcome" src="https://img.shields.io/badge/PRs-welcome-blueviolet.svg">
</p>

<p align="center">
  <a href="#why-this-exists">Why</a> •
  <a href="#architecture">Architecture</a> •
  <a href="#what-your-agent-can-do">Features</a> •
  <a href="#the-skills">Skills</a> •
  <a href="#quick-start">Install</a> •
  <a href="#supported-unreal-engine-versions">Versions</a>
</p>

---

## Why this exists

There's no shortage of AI assistants for Unreal Engine, but most of them share the same design: a chat window bolted into the editor, a subscription with metered credits, and a cloud service your project gets routed through. We wanted the opposite, so we built it.

UE-AgentFramework takes a different bet. Agent platforms like Claude Code, Antigravity, and Codex have spent years getting multi-step reasoning, context management, and self-correction right. Rather than reimplementing all of that in an editor plugin, we hand those agents a deep, carefully designed set of engine tools over MCP and let them do what they're good at. When the platforms improve, your Unreal workflow improves with them — we don't have to ship anything.

A few things we care about and think you will too:

- **It's free and it stays free.** MIT license, no credit meter, no account. Audit it, fork it, extend it.
- **It understands your C++, not just your files.** A dedicated Clang-based AST server resolves class hierarchies, method signatures, property types, and transitive call graphs across your whole codebase.
- **It finds things by meaning.** Blueprint semantic search ("the Blueprint that handles player health"), semantic UE documentation lookup, and an auto-generated project index.
- **It's frugal with your context window.** Tool responses are compacted and hashed, and tool schemas load lazily by category, so the agent spends its tokens on your problem instead of on boilerplate.
- **Your project stays home.** No mandatory cloud round-trip. Run against local or self-hosted models if that's your setup.

---

## Architecture

Instead of embedding a chat UI in the editor, we expose the editor to external agents through two MCP servers and a bridge:

```
┌─────────────────────────────────┐     ┌──────────────────────────────────┐
│  Your AI Agent                  │     │  Unreal Engine Editor            │
│  (Antigravity / Claude Code /   │◄───►│                                  │
│   Codex / Kilo / any MCP client)│ MCP │  ┌────────────────────────────┐  │
│                                 │     │  │ AgentFramework C++ Plugin  │  │
│  ┌───────────────────────────┐  │     │  │ Internal MCP Server :18777 │  │
│  │ Agent Skills & Rules      │  │     │  │ 150+ Game-Thread Tools     │  │
│  │ System Prompt & Profiles  │  │     │  └────────────────────────────┘  │
│  └───────────────────────────┘  │     │                                  │
│                                 │     └──────────────────────────────────┘
│  ┌───────────────────────────┐  │
│  │ External Python AST       │  │
│  │ Server & Bridge Proxy     │  │
│  │ • C++ Symbol Resolution   │  │
│  │ • Call-Graph Analysis     │  │
│  │ • Semantic Vector Search  │  │
│  │ • Blueprint / Docs Index  │  │
│  └───────────────────────────┘  │
└─────────────────────────────────┘
```

- **Internal C++ MCP server** — an in-editor HTTP loopback server (port `18777` by default, configurable via `-AgentFrameworkPort=` or `AGENTFRAMEWORK_HTTP_PORT`, with automatic endpoint discovery) that runs Game-Thread operations in real time: reading and editing Blueprints, injecting T3D node graphs, compiling, profiling, driving PIE sessions, and everything else in the feature list below.
- **External Python AST server** — parses your project's C++ with Clang and answers questions about hierarchies, signatures, and call graphs. Also hosts the vector indexes for Blueprint and documentation search. This is the layer that separates *understanding* a project from grepping it.
- **MCP bridge proxy** — translates MCP JSON-RPC between the agent platform and both servers, with per-client tool profiles and schema caching.

Before touching your code, the agent builds real context: the `unreal-setup` skill scans the project once and generates a compact index of your classes, Blueprints, assets, and architectural patterns, stored as a skill that loads every session.

---

## What your agent can do

The plugin exposes over 150 tools to the agent, and at this point the coverage reaches into pretty much every corner of the engine — from Blueprint graphs and Live Coding all the way out to Motion Matching, Mass crowds, Chaos destruction, and the Movie Render Pipeline.

| Area | What agents can do |
|---|---|
| **Blueprints** | Create and edit graphs via T3D node injection, manage components (SCS), variables, functions, and events, wire pins, set defaults, run batch transactions, apply structural modification ops, export graph summaries, verify connections, check asset state, and read compile diagnostics |
| **C++** | Scaffold classes with header + source, edit files, trigger Live Coding / Hot Reload, regenerate project files, query UHT reflection — backed by the Clang AST server for symbol resolution and call-graph analysis |
| **Animation & rigging** | Create Anim Blueprints, Montages, and Blend Spaces, import FBX animations, build Control Rigs, IK Rigs, and IK Retargeters, configure Motion Matching (PoseSearch), set up Motion Warping, and map Live Link sources |
| **Game AI** | Create Blackboards, Behavior Trees (with node-graph injection), and StateTrees, set up Mass spawners, traits, and crowds, query Smart Objects, run EQS queries, and configure NavMesh |
| **World building** | Create Landscapes and landscape grass types, define foliage types and paint them with a brush, configure World Partition, create Level Instances and Packed Level Actors, spawn actors, place lights, and edit World Settings |
| **Geometry & physics** | Generate Dynamic Meshes through Geometry Scripting, set up Chaos physics and Dataflow destruction graphs, configure cloth simulation, build Chaos Vehicles, audit Nanite settings, and set up Runtime Virtual Textures and Sparse Volume Textures |
| **Niagara VFX** | Create systems, emitters, effect types, and data channels; insert and configure script modules, DI assignments, and multi-channel curves; compile; and capture temporal keyframe preview grids for visual QA |
| **Procedural Content (PCG)** | Create graphs, wire nodes, attach components, set parameters, run local generation, and introspect graph structure |
| **Materials** | Create Materials and Instances, add expression nodes, wire outputs to material properties, and render preview captures for visual verification |
| **UMG / UI** | Build widget hierarchies from 30+ widget types, configure slots, anchors, and alignment, set fonts and brushes, bind events, instantiate full UIs from JSON, and capture the result |
| **Gameplay Ability System** | Register Gameplay Tags, create Attribute Sets, configure ASCs, and author Gameplay Effects and Abilities |
| **Enhanced Input** | Create Input Actions and Mapping Contexts, add key and button mappings, and wire up C++ or Blueprint bindings |
| **Cinematics & rendering** | Create Level Sequences, add tracks and keyframes, set up cine camera rig rails, configure Movie Render Pipeline jobs, patch DMX fixtures, tune Lumen GI and reflections, and configure HLOD |
| **Data** | Create Data Tables and Data Assets, import JSON, instantiate inline subobjects, and set or introspect properties across any UObject |
| **Multiplayer** | Guided setup for replicated properties, RepNotify, Server/Client RPCs, and DOREPLIFETIME conditions |
| **Performance** | Read memory and frame-timing stats, run stat commands, analyze asset sizes, drive the CSV profiler, get/set/discover CVars, and adjust scalability and renderer settings |
| **Autonomous playtesting** | Start and stop PIE, simulate keyboard and gamepad input, extract the full UMG/Slate widget tree as JSON, click UI elements, and query world actors by class, tag, or radius |
| **Import & assets** | Create arbitrary engine assets (PhysicalMaterial, CurveFloat, InputMappingContext, etc.), import FBX/OBJ as static or skeletal meshes, batch-import textures and audio, and configure Nanite, LODs, collision, and lightmaps |
| **Generative assets** | Generate 3D models (Meshy) and voice/audio (ElevenLabs) from a prompt, then import them and wire up PBR materials automatically — via the `generate-assets` skill |
| **Epic's AI Assistant** | Query the native UE 5.7/5.8 AI Assistant directly from your agent, so Epic's own documentation engine is one tool call away |
| **Build & ship** | Build lighting (Preview through Production), package for Win64/Linux/Mac/Android/iOS, validate assets for cook errors, and run UE Automation Tests |
| **Source control** | Status, checkout, add, and revert for Perforce or Git |
| **Editor & Python** | Execute Python in the editor, capture viewport screenshots, read the message log, and run console commands |

Tool schemas are grouped into categories and loaded lazily — the agent activates a category (say, `animation` or `pcg`) only when the task calls for it, which keeps sessions lean even with this much surface area.

---

## The skills

Alongside the tools, the plugin ships 13 skills — structured playbooks the agent loads on demand instead of rediscovering workflows from scratch:

| Skill | Purpose |
|---|---|
| `unreal-setup` | One-time project scan → generates the machine-specific env and a permanent `project-index` skill |
| `unreal-instructions` | Entry point and routing guide for the Dual-MCP ecosystem and tool selection |
| `blueprint-authoring` | T3D rules, layout formatting, and procedures for creating and modifying Blueprint graphs |
| `niagara-authoring` | Create, modify, compile, and visually evaluate Niagara systems |
| `generate-assets` | Generate 3D models and audio via AI APIs (Meshy, ElevenLabs) and import them into the project |
| `create-actor` | Scaffold a complete Actor with C++ class and optional Blueprint child |
| `create-interface` | Create a UInterface with C++ backing for interactable objects |
| `add-component` | Add and configure a component on an existing actor class |
| `setup-input` | Configure Enhanced Input with Actions and Mapping Contexts |
| `setup-replication` | Configure an actor for multiplayer network replication |
| `pie-verifier` | Autonomously run PIE, navigate UI, click elements, and query world state |
| `unreal-testing-sops` | Minimal-token procedures for reliably testing game functionality |
| `python-env` | Python environment, test execution, and IPC troubleshooting |

---

## Supported Unreal Engine versions

- **Unreal Engine 5.8 (recommended)** — full support; integrates with and extends the official Epic Games UE MCP server.
- **Unreal Engine 5.7** — tested and fully supported.
- **Other UE 5.x versions** — likely to work, but not actively tested.

---

## Quick start

An automated PowerShell installer handles file copying, the Python virtual environment, dependencies, and configuration for whichever assistants you use.

### Requirements

- **Windows.** The installer is PowerShell-based; Mac/Linux isn't supported yet.
- **Unreal Engine 5.7 or 5.8**, with Visual Studio set up for C++ development (you need this anyway to build the engine).
- **Python 3.x** on your `PATH` (used for the AST server and MCP bridge's virtual environment).
- **LLVM/Clang**, for the AST server to parse your project's C++. The easiest way to get it: open the **Visual Studio Installer**, edit your installation, and check **C++ Clang tools for Windows** under the Desktop C++ workload — since you already have Visual Studio installed for UE, this is a single checkbox. A standalone install from [llvm.org](https://llvm.org) also works.
- **Git**, if you want the source-control tools to have something to talk to.

### 1. Install

1. **Close the Unreal Editor** so plugin files can be copied safely.
2. **Run the installer** from this repository's root:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\UnrealEngine\install.ps1
   ```
   *(If it fails due to Windows security policy, run it from a terminal rather than double-clicking.)*
3. **Follow the prompts** — enter the absolute path to your target UE project and pick one or more AI assistants (comma-separated, e.g. `0,2`, or `all`). You can re-run the installer anytime to add more assistants.
4. **Launch the Editor** and choose **Yes** if prompted to compile missing modules for the `AgentFramework` plugin. This starts the internal loopback server.

### 2. Initialize your AI workspace

Open your AI assistant in your target UE project folder and send:

> `"Run the unreal-setup skill to configure the project"`

The agent scans your C++ codebase, configures the local environment, and builds the project index. Restart your assistant afterward so it picks up the newly generated skills.

---

---

## Repository structure

- **`AgentFramework/`** — the Unreal Engine C++ editor plugin. Runs the internal MCP server with 150+ Game-Thread tools.
- **`UnrealEngine/`** — the agent-side plugin: MCP bridge proxy, Python AST server, the 13 skills, system prompts, and agent profiles.
- **`Tests/`** — a `pytest`-based integration and unit suite, test runners, and benchmark baseline telemetry (`benchmark_baseline.json`).

---

## Building the C++ Plugin

Unreal Engine compilation relies on UBT. To build the C++ plugin headlessly and prevent UAT instance lock conflicts, set the mutex bypass environment variable before building:
```powershell
$env:uebp_UATMutexNoWait = '1'
powershell -ExecutionPolicy Bypass -File .\build_plugin.ps1 -NoZip
```

---

## Lineage & acknowledgments

UE-AgentFramework started as a fork of the open-source [Autonomix](https://github.com/PRQELT/Autonomix) plugin by QXMP Labs, whose native C++ in-editor tool foundation pioneered T3D injection and execution validation. On top of that foundation we added:

- the external Python Clang AST server for semantic code intelligence,
- vector databases for Blueprint and UE documentation search,
- the MCP bridge proxy that detaches the agent loop from the editor and enables the bring-your-own-agent model,
- and the skill system for token-efficient playbook execution.

Thanks to the Autonomix maintainers for the excellent groundwork.

---

## Contributing

Issues, feature requests, and pull requests are all welcome — whether it's a new engine subsystem, a token-efficiency improvement, or better support for another agent platform.

## License

Released under the [MIT License](LICENSE).
