---
name: generate-assets
description: Generates 3D models and audio assets via AI APIs (Meshy, ElevenLabs) and imports them into Unreal Engine.
argument-hint: "What you want to generate (e.g. '3D model of a rusty barrel', 'Audio of a villain laughing')"
---

# Generative AI Workflow

You have access to a Python utility script `generative_utils.py` located inside the plugin's ExternalServer directory. This script securely interfaces with third-party generative AI services to create assets for the Unreal Engine project.

## Requirements

The user MUST provide the required API keys in a `.env` file located at the root of the project.
- For 3D Models (Meshy): `MESHY_API_KEY`
- For Audio (ElevenLabs): `ELEVENLABS_API_KEY`

If the script fails due to a missing key, kindly ask the user to add the key to the `.env` file in the project workspace.

## Step 1: Generate the Asset

To generate an asset, you will run the Python script using a local terminal. The script will poll the API, download the asset, and save it.

Resolve the path to `generative_utils.py` dynamically:
- Script location: `{AGENT_PLUGIN_DIR}/ExternalServer/src/generative_utils.py`
- If `{AGENT_PLUGIN_DIR}` is not in your environment, use `.agents/plugins/UnrealEngine/ExternalServer/src/generative_utils.py` relative to the project root.

**For 3D Models:**
```bash
python "{AGENT_PLUGIN_DIR}/ExternalServer/src/generative_utils.py" --project-root "{PROJECT_ROOT}" meshy --prompt "Your detailed prompt here"
```
*Note: This will download an .fbx file and its associated PBR texture maps (.png).*

**For Audio / Text-to-Speech:**
```bash
python "{AGENT_PLUGIN_DIR}/ExternalServer/src/generative_utils.py" --project-root "{PROJECT_ROOT}" elevenlabs --prompt "Text to speak"
```
*Note: This will download a proper RIFF/WAV file.*

## Step 2: Import into Unreal Engine (UE 5.9 Native Toolsets)

Once the Python script succeeds, it will print the absolute paths of the downloaded files. Import these files into Unreal Editor via `call_tool` with module-qualified toolset names (`<Module>.<Toolset>`, use `describe_tools` if you need full schemas):

**For 3D Models:**
1. Call `EditorToolset.StaticMeshToolset.ImportFile` (`FolderPath: "/Game/GenAI/Meshes"`, `AssetName`, `SourceFile`, `bImportMaterials`, `bImportTextures`, `bReplaceExisting: true`, `bSave: true`) for static meshes, or `EditorToolset.SkeletalMeshToolset.ImportFile` for rigged meshes.
2. Call `EditorToolset.TextureToolset.ImportFile` (`FolderPath: "/Game/GenAI/Textures"`, `AssetName`, `SourceFile`, `bReplaceExisting: true`, `bSave: true`) for each downloaded texture map (`base_color`, `normal`, `roughness`, `metallic`, `ao`).

**For Audio (.wav):**
1. Call `EditorToolset.SoundWaveToolset.ImportFile` (`FolderPath: "/Game/GenAI/Audio"`, `AssetName`, `SourceFile`, `bReplaceExisting: true`, `bSave: true`).

## Step 3: PBR Material Setup (3D Models Only)

After importing the mesh and texture maps, build and assign the material using `EditorToolset.MaterialToolset`, `EditorToolset.MaterialInstanceToolset`, `EditorToolset.ObjectToolset`, `EditorToolset.StaticMeshToolset`, and `EditorToolset.AssetToolset`:

1. **Create & Author PBR Material Graph (`EditorToolset.MaterialToolset`)**:
   - Create the material asset with `EditorToolset.MaterialToolset.CreateMaterial` (`FolderPath: "/Game/GenAI/Materials"`, `AssetName: "M_GenAI_{Name}"`).
   - Find `TextureSample` node class via `EditorToolset.MaterialToolset.ListExpressionClasses` (`Search: "TextureSample"`) and add sample nodes via `EditorToolset.MaterialToolset.AddExpression`.
   - Assign each imported `UTexture2D` asset via `EditorToolset.ObjectToolset.SetProperties` on each expression node, wire expressions to `MP_BaseColor`, `MP_Normal`, `MP_Roughness`, `MP_Metallic`, and `MP_AmbientOcclusion` via `EditorToolset.MaterialToolset.ConnectToOutput`, and compile with `EditorToolset.MaterialToolset.Recompile`.
   - *(Alternatively, if your project already has a master PBR Material, create an instance via `EditorToolset.MaterialInstanceToolset.Create` and bind maps using `EditorToolset.MaterialInstanceToolset.SetTextureParameter`.)*
2. **Assign Material & Save (`EditorToolset.StaticMeshToolset` & `EditorToolset.AssetToolset`)**:
   - Query slot names with `EditorToolset.StaticMeshToolset.GetMaterialSlots`, assign `/Game/GenAI/Materials/M_GenAI_{Name}` via `EditorToolset.StaticMeshToolset.SetMaterial` (`Mesh`, `SlotName`, `Material`), and persist with `EditorToolset.AssetToolset.SaveAsset`.

## Step 4: Audio Asset Configuration (Audio Assets Only)

After importing `.wav` files into `/Game/GenAI/Audio/` via `EditorToolset.SoundWaveToolset.ImportFile`:

1. **Configure `USoundWave` Properties (`EditorToolset.ObjectToolset` & `EditorToolset.AssetToolset`)**:
   - Inspect available properties on the imported `USoundWave` with `EditorToolset.ObjectToolset.ListProperties` or `EditorToolset.ObjectToolset.GetPropertySchema`.
   - Set playback properties (such as `bLooping`, `Volume`, `Pitch`, and `AttenuationSettings`) using `EditorToolset.ObjectToolset.SetProperties` and persist with `EditorToolset.AssetToolset.SaveAsset`.
   - For procedural or spatialized audio synthesis graphs, use `MetaSoundToolset.MetaSoundAuthoringToolset` (`describe_toolsets` with `["MetaSoundToolset.MetaSoundAuthoringToolset"]`).
