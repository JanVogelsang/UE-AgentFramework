import os
import sys
import re
import logging
from pathlib import Path

# Setup sys.path to find ExternalServer modules
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
EXTERNAL_SERVER_DIR = REPO_ROOT / "ExternalServer" / "src"
if str(EXTERNAL_SERVER_DIR) not in sys.path:
    sys.path.insert(0, str(EXTERNAL_SERVER_DIR))

try:
    import vector_store
except ImportError:
    from ExternalServer.src import vector_store

logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger("IndexPythonAPI")

def find_python_stub_path() -> Path:
    """Finds unreal.pyi or unreal.py in target projects or Engine Intermediate directories."""
    candidates = []
    
    user_profile = os.environ.get("USERPROFILE", "")
    projects_dir = Path(user_profile) / "Documents" / "Unreal Projects" if user_profile else Path("C:/Users")
    
    candidate_dirs = [
        REPO_ROOT.parent / "AgentFrameworkTest" / "Intermediate" / "PythonStub",
        projects_dir / "AgentFrameworkTest" / "Intermediate" / "PythonStub",
        REPO_ROOT / "Intermediate" / "PythonStub",
        Path("D:/UE_5.9/Engine/Intermediate/PythonStub"),
        Path("C:/Program Files/Epic Games/UE_5.9/Engine/Intermediate/PythonStub"),
        Path("D:/Epic Games/UE_5.9/Engine/Intermediate/PythonStub"),
    ]
    
    # Also scan any subfolders in Unreal Projects
    if projects_dir.exists():
        for p in projects_dir.iterdir():
            if p.is_dir():
                candidate_dirs.append(p / "Intermediate" / "PythonStub")
                
    for c_dir in candidate_dirs:
        pyi = c_dir / "unreal.pyi"
        py = c_dir / "unreal.py"
        if pyi.exists():
            return pyi
        if py.exists():
            return py
            
    return None

def parse_python_stubs(stub_path: Path):
    """Parses Python stub file into class/function documentation chunks."""
    logger.info(f"Reading stub file: {stub_path}...")
    with open(stub_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    logger.info("Parsing classes and functions from stub content...")
    
    # Regex pattern for class declarations and top-level functions
    class_pattern = re.compile(r"^class\s+([A-Za-z0-9_]+)(?:\(([^)]*)\))?:", re.MULTILINE)
    
    matches = list(class_pattern.finditer(content))
    logger.info(f"Found {len(matches)} class definitions in stubs.")
    
    documents = []
    metadatas = []
    ids = []
    
    for i, match in enumerate(matches):
        class_name = match.group(1)
        bases = match.group(2) or ""
        
        start_pos = match.start()
        end_pos = matches[i + 1].start() if i + 1 < len(matches) else len(content)
        
        class_body = content[start_pos:end_pos]
        
        # Limit body length per chunk (keep signatures & docstrings)
        lines = class_body.splitlines()[:60] # First 60 lines of class declaration
        chunk_text = f"Unreal Python API Class: unreal.{class_name}\nInherits: {bases}\n\nDefinition:\n" + "\n".join(lines)
        
        doc_id = f"py_stub_{class_name}"
        documents.append(chunk_text)
        metadatas.append({
            "title": f"unreal.{class_name}",
            "source": "unreal.pyi",
            "type": "python_api",
            "class_name": class_name
        })
        ids.append(doc_id)
        
        # Process in batches of 500
        if len(documents) >= 500:
            logger.info(f"Ingesting batch of {len(documents)} stubs into vector store...")
            vector_store.upsert_documents(documents, metadatas, ids)
            documents, metadatas, ids = [], [], []
            
    if documents:
        logger.info(f"Ingesting final batch of {len(documents)} stubs into vector store...")
        vector_store.upsert_documents(documents, metadatas, ids)
        
    logger.info("Python stubs indexing completed.")

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Index Unreal Engine Python API stubs into ChromaDB.")
    parser.add_argument("--stub-path", type=str, help="Path to unreal.pyi or unreal.py stub file.")
    parser.add_argument("--ue-version", type=str, default="5.9", help="Unreal Engine version (default: 5.9).")
    args = parser.parse_args()
    
    stub_path = Path(args.stub_path) if args.stub_path else find_python_stub_path()
    if not stub_path or not stub_path.exists():
        logger.error("Could not locate unreal.pyi or unreal.py stub file. Ensure Unreal Engine Editor has run with PythonScriptPlugin enabled.")
        sys.exit(1)
        
    ue_version = args.ue_version
    vector_db_dir = REPO_ROOT / "UnrealEngine" / "ExternalServer" / f"vector_db_{ue_version}"
    logger.info(f"Initializing vector store at {vector_db_dir}...")
    
    vector_store.initialize_db(str(vector_db_dir), ue_version)
    if not vector_store.wait_for_initialization(timeout=30.0):
        logger.error("Vector store failed to initialize within timeout.")
        sys.exit(1)
        
    parse_python_stubs(stub_path)
    logger.info("Successfully indexed Unreal Engine Python API!")

if __name__ == "__main__":
    main()
