import os
import re
import sys
import argparse
import time
from pathlib import Path
from typing import List, Dict, Any
from concurrent.futures import ThreadPoolExecutor, as_completed

# Disable ChromaDB telemetry before importing it
os.environ["CHROMA_TELEMETRY"] = "false"
os.environ["ANONYMIZED_TELEMETRY"] = "False"

# Ensure import path includes src/
sys.path.append(str(Path(__file__).resolve().parent.parent))

try:
    import chromadb
    from chromadb.utils.embedding_functions import ONNXMiniLM_L6_V2
except ImportError as e:
    print(f"Error: Required library is missing. Install requirements.txt first. Details: {e}")
    sys.exit(1)

def parse_header_file(file_path: Path) -> List[Dict[str, Any]]:
    chunks = []
    try:
        content = file_path.read_text(encoding="utf-8", errors="ignore")
    except Exception as e:
        print(f"Failed to read {file_path}: {e}")
        return chunks

    lines = content.splitlines()
    in_comment = False
    comment_lines = []
    
    # Class tracking
    current_class_name = "Global"
    
    # Regular expressions
    uclass_pat = re.compile(r"UCLASS\s*\(")
    ustruct_pat = re.compile(r"USTRUCT\s*\(")
    uenum_pat = re.compile(r"UENUM\s*\(")
    ufunction_pat = re.compile(r"UFUNCTION\s*\(")
    uproperty_pat = re.compile(r"UPROPERTY\s*\(")
    
    class_decl_pat = re.compile(r"\bclass\s+[A-Z0-9_]+_API\s+([A-Za-z0-9_]+)")
    struct_decl_pat = re.compile(r"\bstruct\s+[A-Z0-9_]+_API\s+([A-Za-z0-9_]+)")
    enum_decl_pat = re.compile(r"\benum\s+(?:class\s+)?([A-Za-z0-9_]+)")
    
    i = 0
    num_lines = len(lines)
    while i < num_lines:
        line = lines[i].strip()
        
        # 1. Comment block handling
        if line.startswith("/**"):
            in_comment = True
            comment_lines = []
            stripped = line[3:].strip()
            if stripped:
                comment_lines.append(stripped)
            if "*/" in line:
                in_comment = False
                if comment_lines and comment_lines[-1].endswith("*/"):
                    comment_lines[-1] = comment_lines[-1][:-2].strip()
            i += 1
            continue
            
        if in_comment:
            if "*/" in line:
                in_comment = False
                stripped = line.split("*/")[0].strip()
                if stripped.startswith("*"):
                    stripped = stripped[1:].strip()
                if stripped:
                    comment_lines.append(stripped)
            else:
                stripped = line
                if stripped.startswith("*"):
                    stripped = stripped[1:].strip()
                comment_lines.append(stripped)
            i += 1
            continue
            
        if line.startswith("//"):
            comment_lines.append(line[2:].strip())
            i += 1
            continue

        # 2. Parse reflection macros
        if uclass_pat.search(line):
            decl_line = ""
            for j in range(i + 1, min(i + 10, num_lines)):
                if "class" in lines[j]:
                    decl_line = lines[j]
                    break
            if decl_line:
                match = class_decl_pat.search(decl_line)
                if not match:
                    match = re.search(r"\bclass\s+([A-Za-z0-9_]+)", decl_line)
                if match:
                    current_class_name = match.group(1)
                    desc = " ".join(comment_lines).strip()
                    if desc:
                        chunks.append({
                            "type": "C++ Class / Blueprint Type",
                            "class": current_class_name,
                            "name": current_class_name,
                            "signature": decl_line.strip(),
                            "description": desc,
                            "source": file_path.name
                        })
            comment_lines = []
            
        elif ustruct_pat.search(line):
            decl_line = ""
            for j in range(i + 1, min(i + 10, num_lines)):
                if "struct" in lines[j]:
                    decl_line = lines[j]
                    break
            if decl_line:
                match = struct_decl_pat.search(decl_line)
                if not match:
                    match = re.search(r"\bstruct\s+([A-Za-z0-9_]+)", decl_line)
                if match:
                    struct_name = match.group(1)
                    desc = " ".join(comment_lines).strip()
                    if desc:
                        chunks.append({
                            "type": "C++ Struct",
                            "class": struct_name,
                            "name": struct_name,
                            "signature": decl_line.strip(),
                            "description": desc,
                            "source": file_path.name
                        })
            comment_lines = []
            
        elif uenum_pat.search(line):
            decl_line = ""
            for j in range(i + 1, min(i + 10, num_lines)):
                if "enum" in lines[j]:
                    decl_line = lines[j]
                    break
            if decl_line:
                match = enum_decl_pat.search(decl_line)
                if match:
                    enum_name = match.group(1)
                    desc = " ".join(comment_lines).strip()
                    if desc:
                        chunks.append({
                            "type": "C++ Enum",
                            "class": enum_name,
                            "name": enum_name,
                            "signature": decl_line.strip(),
                            "description": desc,
                            "source": file_path.name
                        })
            comment_lines = []
            
        elif ufunction_pat.search(line):
            macro_content = line
            if "(" in line and ")" not in line:
                for j in range(i + 1, min(i + 5, num_lines)):
                    macro_content += " " + lines[j]
                    if ")" in lines[j]:
                        break
            
            is_blueprint = any(x in macro_content for x in (
                "BlueprintCallable", "BlueprintPure", "BlueprintImplementableEvent", 
                "BlueprintNativeEvent", "BlueprintAuthorityOnly"
            ))
            
            cat_match = re.search(r'Category\s*=\s*"([^"]+)"', macro_content)
            category = cat_match.group(1) if cat_match else "Default"
            
            decl_line = ""
            for j in range(i + 1, min(i + 10, num_lines)):
                l_next = lines[j].strip()
                if not l_next or l_next.startswith("//") or l_next.startswith("/*") or l_next.startswith("UFUNCTION") or l_next.startswith("UPROPERTY"):
                    continue
                decl_line = l_next
                break
                
            if decl_line and is_blueprint:
                cleaned_sig = re.sub(r"\b(virtual|inline|FORCEINLINE)\b", "", decl_line).strip()
                func_match = re.search(r"([A-Za-z0-9_]+)\s*\(", cleaned_sig)
                if func_match:
                    func_name = func_match.group(1)
                    desc = " ".join(comment_lines).strip()
                    access = "BlueprintPure" if "BlueprintPure" in macro_content else "BlueprintCallable"
                    chunks.append({
                        "type": "Blueprint Node / C++ Function",
                        "class": current_class_name,
                        "name": func_name,
                        "category": category,
                        "access": access,
                        "signature": cleaned_sig,
                        "description": desc or f"Exposed function {func_name} in {current_class_name}.",
                        "source": file_path.name
                    })
            comment_lines = []

        elif uproperty_pat.search(line):
            macro_content = line
            if "(" in line and ")" not in line:
                for j in range(i + 1, min(i + 5, num_lines)):
                    macro_content += " " + lines[j]
                    if ")" in lines[j]:
                        break
            
            is_blueprint = any(x in macro_content for x in ("BlueprintReadWrite", "BlueprintReadOnly"))
            cat_match = re.search(r'Category\s*=\s*"([^"]+)"', macro_content)
            category = cat_match.group(1) if cat_match else "Default"
            
            decl_line = ""
            for j in range(i + 1, min(i + 10, num_lines)):
                l_next = lines[j].strip()
                if not l_next or l_next.startswith("//") or l_next.startswith("/*") or l_next.startswith("UFUNCTION") or l_next.startswith("UPROPERTY"):
                    continue
                decl_line = l_next
                break
                
            if decl_line and is_blueprint:
                cleaned_sig = decl_line.strip()
                prop_match = re.search(r"([A-Za-z0-9_]+)\s*(?:;|=|\b)", cleaned_sig)
                if prop_match:
                    prop_name = prop_match.group(1)
                    desc = " ".join(comment_lines).strip()
                    access = "BlueprintReadWrite" if "BlueprintReadWrite" in macro_content else "BlueprintReadOnly"
                    chunks.append({
                        "type": "Blueprint Property",
                        "class": current_class_name,
                        "name": prop_name,
                        "category": category,
                        "access": access,
                        "signature": cleaned_sig,
                        "description": desc or f"Exposed property {prop_name} in {current_class_name}.",
                        "source": file_path.name
                    })
            comment_lines = []

        elif line and not line.startswith("UCLASS") and not line.startswith("USTRUCT") and not line.startswith("UENUM") and not line.startswith("UFUNCTION") and not line.startswith("UPROPERTY"):
            if ";" in line or "{" in line or "class " in line or "struct " in line:
                comment_lines = []
                
        i += 1
    return chunks

def format_chunk(c: Dict[str, Any]) -> str:
    """Formats the extracted C++/Blueprint metadata into a clean documentation block."""
    if c["type"] == "Blueprint Node / C++ Function":
        return (
            f"Type: Blueprint Node / C++ Function\n"
            f"Class: {c['class']}\n"
            f"Name: {c['name']}\n"
            f"Category: {c['category']}\n"
            f"Access: {c['access']}\n"
            f"Signature: {c['signature']}\n"
            f"Description: {c['description']}"
        )
    elif c["type"] == "Blueprint Property":
        return (
            f"Type: Blueprint Property\n"
            f"Class: {c['class']}\n"
            f"Name: {c['name']}\n"
            f"Category: {c['category']}\n"
            f"Access: {c['access']}\n"
            f"Signature: {c['signature']}\n"
            f"Description: {c['description']}"
        )
    else:
        return (
            f"Type: {c['type']}\n"
            f"Name: {c['name']}\n"
            f"Signature: {c['signature']}\n"
            f"Description: {c['description']}"
        )

def main():
    parser = argparse.ArgumentParser(description="Parse Unreal Engine source headers and index Blueprint-exposed API reference.")
    parser.add_argument("--source-dir", default="D:\\UE_5.9\\Engine\\Source" if Path("D:\\UE_5.9\\Engine\\Source").exists() else "C:\\Program Files\\Epic Games\\UE_5.9\\Engine\\Source", help="UE engine Source directory path.")
    parser.add_argument("--ue-version", default="5.9", help="Unreal Engine version (for directory naming).")
    parser.add_argument("--output-dir", help="Explicit path to ChromaDB output directory.")
    parser.add_argument("--upload-github", action="store_true", help="Package and upload the final database to GitHub releases.")
    
    args = parser.parse_args()
    
    source_path = Path(args.source_dir)
    if not source_path.exists():
        print(f"Error: Source directory {source_path} does not exist.")
        sys.exit(1)
        
    ue_version = args.ue_version
    if args.output_dir:
        output_path = Path(args.output_dir)
    else:
        output_path = Path(__file__).resolve().parent.parent / f"vector_db_{ue_version}"
        
    print(f"Index scan starting on {source_path}...")
    print(f"Target vector database: {output_path}")
    
    # Exclude directories we don't care about (e.g. thirdparty)
    target_folders = ["Runtime", "Developer", "Plugins"]
    headers_to_scan = []
    
    for sub in target_folders:
        folder_path = source_path / sub
        if folder_path.exists():
            print(f"Scanning subfolder: {folder_path.name}...")
            for p in folder_path.rglob("*.h"):
                path_parts = p.parts
                if any(x in path_parts for x in ("ThirdParty", "Intermediate", "Binaries", "Saved")):
                    continue
                headers_to_scan.append(p)
                
    total_headers = len(headers_to_scan)
    print(f"Discovered {total_headers} header files to parse.")
    
    # Parse headers in parallel
    print("Parsing headers...")
    start_time = time.time()
    all_extracted_chunks = []
    
    max_workers = max(1, os.cpu_count() - 1)
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(parse_header_file, path): path for path in headers_to_scan}
        processed = 0
        for future in as_completed(futures):
            processed += 1
            chunks = future.result()
            if chunks:
                all_extracted_chunks.extend(chunks)
            if processed % 2000 == 0:
                print(f"Parsed {processed}/{total_headers} headers...")
                
    elapsed = time.time() - start_time
    print(f"Finished parsing {total_headers} headers in {elapsed:.2f}s. Extracted {len(all_extracted_chunks)} API/Blueprint chunks.")
    
    if not all_extracted_chunks:
        print("No Blueprint-exposed elements found. Ingestion skipped.")
        return
        
    # Ingest into ChromaDB
    print("Connecting to ChromaDB and embedding chunks...")
    settings = chromadb.Settings(anonymized_telemetry=False)
    client = chromadb.PersistentClient(path=str(output_path), settings=settings)
    
    embedding_fn = ONNXMiniLM_L6_V2()
    collection = client.get_or_create_collection(
        name="unreal_docs",
        embedding_function=embedding_fn
    )
    
    # Batch indexing
    batch_size = 500
    documents = []
    metadatas = []
    ids = []
    
    chunk_id_counter = collection.count()
    print(f"Existing document count in collection 'unreal_docs': {chunk_id_counter}")
    
    for idx, c in enumerate(all_extracted_chunks):
        doc_str = format_chunk(c)
        documents.append(doc_str)
        metadatas.append({
            "source": c["source"],
            "title": f"API: {c['class']}::{c['name']}",
            "type": c["type"]
        })
        ids.append(f"api_{chunk_id_counter}")
        chunk_id_counter += 1
        
        if len(ids) >= batch_size:
            collection.upsert(
                ids=ids,
                documents=documents,
                metadatas=metadatas
            )
            documents = []
            metadatas = []
            ids = []
            if chunk_id_counter % 2000 == 0:
                print(f"Embedded {chunk_id_counter} documents...")
                
    if ids:
        collection.upsert(
            ids=ids,
            documents=documents,
            metadatas=metadatas
        )
        
    print(f"Ingestion completed. Total document count in collection 'unreal_docs': {collection.count()}")

    if args.upload_github:
        try:
            sys.path.append(str(Path(__file__).resolve().parent))
            from build_vector_db import zip_directory, upload_to_github_release
            zip_path = output_path.parent / f"vector_db_{ue_version}.zip"
            zip_directory(output_path, zip_path)
            upload_to_github_release(zip_path)
        except Exception as e:
            print(f"Failed to package and upload: {e}")

if __name__ == "__main__":
    main()
