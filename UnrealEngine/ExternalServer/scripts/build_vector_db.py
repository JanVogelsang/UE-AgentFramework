import os
import argparse
import sys
import shutil
import zipfile
import subprocess
from pathlib import Path
from typing import List, Dict, Any

# Disable ChromaDB telemetry before importing it
os.environ["CHROMA_TELEMETRY"] = "false"
os.environ["ANONYMIZED_TELEMETRY"] = "False"

# Ensure import path includes src/
sys.path.append(str(Path(__file__).resolve().parent.parent))

try:
    import chromadb
    from chromadb.utils.embedding_functions import ONNXMiniLM_L6_V2
    from pypdf import PdfReader
except ImportError as e:
    print(f"Error: Required library is missing. Install requirements.txt first. Details: {e}")
    sys.exit(1)

def zip_directory(directory_path: Path, output_zip_path: Path):
    """
    Zips the directory at directory_path to output_zip_path.
    """
    print(f"Zipping {directory_path} to {output_zip_path}...")
    base_name = str(output_zip_path.with_suffix(""))
    shutil.make_archive(base_name, 'zip', root_dir=directory_path.parent, base_dir=directory_path.name)

def upload_to_github_release(zip_path: Path):
    """
    Uploads the zip file to the GitHub release tag 'vector-dbs'.
    """
    print("Checking if GitHub CLI (gh) is available...")
    try:
        subprocess.run(["gh", "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("Error: GitHub CLI (gh) is not installed or not in PATH. Please install it to upload.")
        return False

    res = subprocess.run(["gh", "auth", "status"], capture_output=True, text=True)
    if res.returncode != 0:
        print("Error: GitHub CLI is not authenticated. Please run 'gh auth login' first.")
        return False

    print("Verifying GitHub release 'vector-dbs' exists...")
    res = subprocess.run(["gh", "release", "view", "vector-dbs"], capture_output=True, text=True)
    if res.returncode != 0:
        print("Release 'vector-dbs' not found. Creating it...")
        create_res = subprocess.run(
            ["gh", "release", "create", "vector-dbs", "--notes", "Pre-compiled vector databases for Unreal Engine conceptual guides and API references.", "--title", "Vector Databases"],
            capture_output=True,
            text=True
        )
        if create_res.returncode != 0:
            print(f"Failed to create release: {create_res.stderr}")
            return False

    print(f"Uploading {zip_path.name} to release 'vector-dbs'...")
    upload_res = subprocess.run(
        ["gh", "release", "upload", "vector-dbs", str(zip_path), "--clobber"],
        capture_output=True,
        text=True
    )
    if upload_res.returncode == 0:
        print(f"Successfully uploaded {zip_path.name} to GitHub release 'vector-dbs'.")
        return True
    else:
        print(f"Failed to upload asset: {upload_res.stderr}")
        return False

def extract_pdf_text(pdf_path: Path) -> List[Dict[str, Any]]:
    """
    Extracts text page by page from a PDF file.
    """
    print(f"Extracting text from PDF: {pdf_path}")
    reader = PdfReader(pdf_path)
    pages_content = []
    for idx, page in enumerate(reader.pages):
        text = page.extract_text()
        if text and text.strip():
            pages_content.append({
                "content": text.strip(),
                "page_num": idx + 1
            })
    return pages_content

def chunk_text(text: str, max_chars: int = 800, overlap: int = 100) -> List[str]:
    """
    Splits text into chunks of max_chars with overlap.
    Limits chunk size to guarantee it fits within the 256-token context window of all-MiniLM-L6-v2.
    """
    chunks = []
    start = 0
    text_len = len(text)
    
    while start < text_len:
        end = min(start + max_chars, text_len)
        if end < text_len:
            # Find last space to avoid cutting words
            last_space = text.rfind(" ", start, end)
            if last_space > start + (max_chars // 2):
                end = last_space
        chunks.append(text[start:end].strip())
        start = end - overlap
        if start < 0 or start >= text_len or end >= text_len:
            break
            
    return [c for c in chunks if len(c) > 50]

def main():
    parser = argparse.ArgumentParser(description="Build versioned ChromaDB for Unreal Engine documentation.")
    parser.add_argument("--docs-dir", required=True, help="Directory containing PDFs or Markdown/Text documentation files.")
    parser.add_argument("--ue-version", default="5.9", help="Unreal Engine version (e.g. 5.9) for output directory naming.")
    parser.add_argument("--output-dir", help="Explicit path to output directory (overrides default naming).")
    parser.add_argument("--upload-github", action="store_true", help="Zip the output vector database and upload it to GitHub release 'vector-dbs'.")
    
    args = parser.parse_args()
    
    docs_path = Path(args.docs_dir)
    if not docs_path.exists():
        print(f"Error: Input directory {docs_path} does not exist.")
        sys.exit(1)
        
    ue_version = args.ue_version
    if args.output_dir:
        output_path = Path(args.output_dir)
    else:
        output_path = Path(__file__).resolve().parent.parent / f"vector_db_{ue_version}"
        
    print(f"Creating/updating vector database at: {output_path}")
    
    # Initialize ChromaDB client with telemetry disabled
    settings = chromadb.Settings(anonymized_telemetry=False)
    client = chromadb.PersistentClient(path=str(output_path), settings=settings)
    
    embedding_fn = ONNXMiniLM_L6_V2()
    collection = client.get_or_create_collection(
        name="unreal_docs",
        embedding_function=embedding_fn
    )
    
    # Scan files
    pdf_files = list(docs_path.rglob("*.pdf"))
    md_files = list(docs_path.rglob("*.md"))
    txt_files = list(docs_path.rglob("*.txt"))
    
    all_files = pdf_files + md_files + txt_files
    print(f"Found {len(all_files)} documents to ingest ({len(pdf_files)} PDFs, {len(md_files)} Markdown files, {len(txt_files)} text files).")
    
    chunk_id_counter = collection.count()
    
    for file_path in all_files:
        try:
            print(f"Processing {file_path.name}...")
            chunks = []
            metadata_list = []
            
            if file_path.suffix.lower() == ".pdf":
                pages = extract_pdf_text(file_path)
                for page in pages:
                    page_chunks = chunk_text(page["content"])
                    for chunk in page_chunks:
                        chunks.append(chunk)
                        metadata_list.append({
                            "source": file_path.name,
                            "title": file_path.stem,
                            "page": page["page_num"]
                        })
            else:
                content = file_path.read_text(encoding="utf-8", errors="ignore")
                file_chunks = chunk_text(content)
                for chunk in file_chunks:
                    chunks.append(chunk)
                    metadata_list.append({
                        "source": file_path.name,
                        "title": file_path.stem,
                        "page": 1
                    })
                    
            if not chunks:
                continue
                
            ids = [f"doc_{chunk_id_counter + i}" for i in range(len(chunks))]
            chunk_id_counter += len(chunks)
            
            # Batch upsert to ChromaDB
            collection.upsert(
                ids=ids,
                documents=chunks,
                metadatas=metadata_list
            )
            print(f"Ingested {len(chunks)} chunks from {file_path.name}.")
            
        except Exception as e:
            print(f"Failed to process {file_path.name}: {e}")
            
    print(f"Ingestion completed. Total document count in collection 'unreal_docs': {collection.count()}")

    if args.upload_github:
        zip_path = output_path.parent / f"vector_db_{ue_version}.zip"
        zip_directory(output_path, zip_path)
        upload_to_github_release(zip_path)

if __name__ == "__main__":
    main()
