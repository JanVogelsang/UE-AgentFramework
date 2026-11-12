import os
import shutil
import tempfile
import pytest
import sys
from pathlib import Path

# Ensure UnrealEngine folder is in sys.path
sys.path.append(str(Path(__file__).resolve().parent.parent / "UnrealEngine"))

from ExternalServer.src import vector_store

@pytest.fixture
def temp_vector_db():
    temp_dir = tempfile.mkdtemp()
    yield temp_dir
    if os.path.exists(temp_dir):
        try:
            shutil.rmtree(temp_dir)
        except Exception:
            pass

def test_vector_store_not_initialized():
    # If not initialized, count should be 0 and search should return empty list
    vector_store._client = None
    vector_store._collection = None
    assert vector_store.get_document_count() == 0
    results = vector_store.semantic_search("test")
    assert results == []

def test_vector_store_e2e(temp_vector_db):
    chromadb = pytest.importorskip("chromadb")
    from chromadb.utils.embedding_functions import ONNXMiniLM_L6_V2
    
    # Create the test db and pre-populate it (mimicking the dev script)
    client = chromadb.PersistentClient(path=temp_vector_db)
    embedding_fn = ONNXMiniLM_L6_V2()
    collection = client.create_collection(
        name="unreal_docs",
        embedding_function=embedding_fn
    )
    
    collection.upsert(
        ids=["test_doc_1"],
        documents=["Unreal Engine 5 features an Enhanced Input system that allows modular action bindings."],
        metadatas=[{"title": "Enhanced Input System Guide", "source": "enhanced_input.md"}]
    )
    
    # Initialize vector_store with the populated DB
    success = vector_store.initialize_db(temp_vector_db, "5.8")
    assert success is True
    assert vector_store.wait_for_initialization(timeout=10.0) is True
    assert vector_store.get_document_count() == 1
    
    # Perform search
    results = vector_store.semantic_search("how to bind enhanced input action")
    assert len(results) == 1
    assert results[0]["title"] == "Enhanced Input System Guide"
    assert "Enhanced Input system" in results[0]["content"]
    assert results[0]["similarity_score"] > 0.0
    assert results[0]["source"] == "enhanced_input.md"

def create_mock_zip() -> bytes:
    import io
    import zipfile
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w') as z:
        z.writestr("dummy.txt", "hello world")
    return buf.getvalue()

def test_download_and_extract_db(temp_vector_db):
    import io
    from unittest.mock import patch, MagicMock
    mock_zip_bytes = create_mock_zip()
    
    mock_response = io.BytesIO(mock_zip_bytes)
    
    with patch("urllib.request.urlopen", return_value=mock_response):
        success = vector_store.download_and_extract_db("5.8", temp_vector_db)
        assert success is True
        
        extracted_file = Path(temp_vector_db) / "dummy.txt"
        assert extracted_file.exists()
        assert extracted_file.read_text() == "hello world"

def test_download_and_extract_db_failure(temp_vector_db):
    from unittest.mock import patch
    with patch("urllib.request.urlopen", side_effect=Exception("Connection refused")):
        success = vector_store.download_and_extract_db("5.8", temp_vector_db)
        assert success is False

def test_initialize_db_with_download(temp_vector_db):
    pytest.importorskip("chromadb")
    from unittest.mock import patch
    
    # Reset state
    vector_store._initialized = False
    vector_store._is_initializing = False
    vector_store._loaded_version = None
    
    with patch("ExternalServer.src.vector_store.download_and_extract_db") as mock_download:
        def side_effect(ue_ver, path):
            os.makedirs(path, exist_ok=True)
            return True
        mock_download.side_effect = side_effect
        
        success = vector_store.initialize_db(temp_vector_db, "5.8")
        assert success is True
        assert vector_store.wait_for_initialization(timeout=5.0) is True
        assert vector_store.get_loaded_version() == "5.8"

def test_version_reload():
    from unittest.mock import patch
    # Setup initial loaded version
    vector_store._loaded_version = "5.8"
    vector_store._initialized = True
    
    from ExternalServer.src import main
    
    with patch("ExternalServer.src.main.get_unreal_version", return_value="5.9"), \
         patch("ExternalServer.src.vector_store.initialize_db") as mock_init:
        
        main.check_and_reload_vector_db()
        
        mock_init.assert_called_once()
        args, kwargs = mock_init.call_args
        assert args[1] == "5.9" or kwargs.get("ue_version") == "5.9"
