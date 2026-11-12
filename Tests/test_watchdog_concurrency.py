import os
import sys
import time
import json
import shutil
import sqlite3
import tempfile
import threading
import argparse
from pathlib import Path

# Add project root to sys.path
PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

# Default paths
DEFAULT_WATCH_DIR = Path.home() / "Documents" / "Unreal Projects" / "AgentFrameworkTest" / "Source"
DEFAULT_DB_PATH = PROJECT_ROOT / "UnrealEngine" / "ExternalServer" / "ast_cache.db"

# Global test configuration
NUM_READERS = 5
NUM_WRITERS = 2
STRESS_DURATION = 10.0  # seconds
FILE_OP_DELAY = 0.05   # seconds between rapid file updates

# Results collection
lock_errors = 0
total_reads = 0
total_writes = 0
read_latencies = []
write_latencies = []
lock_lock = threading.Lock()

def log_lock_error():
    global lock_errors
    with lock_lock:
        lock_errors += 1

def log_read(latency):
    global total_reads
    with lock_lock:
        total_reads += 1
        read_latencies.append(latency)

def log_write(latency):
    global total_writes
    with lock_lock:
        total_writes += 1
        write_latencies.append(latency)


def simulate_reader(db_path, stop_event):
    """Simulates the MCP query_cpp_ast reader thread."""
    while not stop_event.is_set():
        start_time = time.time()
        conn = None
        try:
            conn = sqlite3.connect(db_path, timeout=5.0)
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            # Simple query mimicking query_cpp_ast looking up a symbol
            cursor.execute("""
                SELECT s.*, f.file_path 
                FROM symbols s
                JOIN files f ON s.file_id = f.id
                WHERE s.name = ? OR s.fully_qualified_name = ?
                LIMIT 10;
            """, ("AMorphTargetActor", "AMorphTargetActor"))
            cursor.fetchall()
            
            # Transitive calls simulation
            cursor.execute("""
                SELECT * FROM function_calls LIMIT 5;
            """)
            cursor.fetchall()
            
            latency = time.time() - start_time
            log_read(latency)
        except sqlite3.OperationalError as e:
            if "database is locked" in str(e).lower():
                log_lock_error()
        except Exception:
            pass
        finally:
            if conn:
                conn.close()
        time.sleep(0.01)  # tiny sleep to prevent CPU hogging


def simulate_writer(db_path, stop_event, writer_id):
    """Simulates the watchdog or background indexing writing thread."""
    file_counter = 0
    while not stop_event.is_set():
        file_counter += 1
        file_path = f"C:/Mock/Path/File_{writer_id}_{file_counter}.cpp"
        mtime = time.time()
        
        start_time = time.time()
        conn = None
        try:
            conn = sqlite3.connect(db_path, timeout=5.0)
            cursor = conn.cursor()
            cursor.execute("PRAGMA foreign_keys = ON;")
            
            # Start write transaction
            cursor.execute("BEGIN IMMEDIATE TRANSACTION;")
            
            # Insert file
            cursor.execute("""
                INSERT OR REPLACE INTO files (file_path, last_parsed_mtime, status)
                VALUES (?, ?, 'parsed');
            """, (file_path, mtime))
            file_id = cursor.lastrowid
            
            # Insert dummy class symbol
            cursor.execute("""
                INSERT INTO symbols (file_id, name, fully_qualified_name, kind, line_start, line_end)
                VALUES (?, ?, ?, 'class', 1, 50);
            """, (file_id, f"Class_{writer_id}_{file_counter}", f"Class_{writer_id}_{file_counter}"))
            class_id = cursor.lastrowid
            
            # Insert dummy properties
            for i in range(5):
                cursor.execute("""
                    INSERT INTO properties (file_id, class_symbol_id, name, type, access_specifier, line)
                    VALUES (?, ?, ?, 'int', 'public', ?);
                """, (file_id, class_id, f"Property_{i}", 10 + i))
                
            # Insert dummy methods
            for i in range(3):
                cursor.execute("""
                    INSERT INTO symbols (file_id, name, fully_qualified_name, kind, parent_symbol_id, line_start, line_end)
                    VALUES (?, ?, ?, 'method', ?, ?, ?);
                """, (file_id, f"Method_{i}", f"Class_{writer_id}_{file_counter}::Method_{i}", class_id, 20 + i, 25 + i))
                
            conn.commit()
            latency = time.time() - start_time
            log_write(latency)
        except sqlite3.OperationalError as e:
            if "database is locked" in str(e).lower():
                log_lock_error()
            if conn:
                try:
                    conn.rollback()
                except Exception:
                    pass
        except Exception:
            if conn:
                try:
                    conn.rollback()
                except Exception:
                    pass
        finally:
            if conn:
                conn.close()
        time.sleep(0.02)


def run_concurrency_stress_test(db_path, use_wal=False):
    """Runs concurrent reader and writer threads against the database."""
    global lock_errors, total_reads, total_writes, read_latencies, write_latencies
    lock_errors = 0
    total_reads = 0
    total_writes = 0
    read_latencies = []
    write_latencies = []

    print(f"\n--- Stressed Concurrency Test (WAL={use_wal}) ---")
    
    # Enable/disable WAL on DB
    conn = sqlite3.connect(db_path)
    if use_wal:
        conn.execute("PRAGMA journal_mode=WAL;")
        mode = conn.execute("PRAGMA journal_mode;").fetchone()[0]
        print(f"Set journal mode to: {mode}")
    else:
        conn.execute("PRAGMA journal_mode=DELETE;")
        mode = conn.execute("PRAGMA journal_mode;").fetchone()[0]
        print(f"Set journal mode to: {mode}")
    conn.close()

    stop_event = threading.Event()
    
    # Spawn threads
    readers = [threading.Thread(target=simulate_reader, args=(db_path, stop_event)) for _ in range(NUM_READERS)]
    writers = [threading.Thread(target=simulate_writer, args=(db_path, stop_event, i)) for i in range(NUM_WRITERS)]
    
    all_threads = readers + writers
    
    for t in all_threads:
        t.start()
        
    print(f"Stressing database for {STRESS_DURATION} seconds with {NUM_READERS} readers and {NUM_WRITERS} writers...")
    time.sleep(STRESS_DURATION)
    
    stop_event.set()
    for t in all_threads:
        t.join()
        
    # Analyze results
    print(f"Total Reads Attempted: {total_reads}")
    print(f"Total Writes Attempted: {total_writes}")
    print(f"Database Lock Errors Encountered: {lock_errors}")
    
    if read_latencies:
        avg_read = sum(read_latencies) / len(read_latencies)
        max_read = max(read_latencies)
        print(f"Read Latency: Avg={avg_read*1000:.2f}ms, Max={max_read*1000:.2f}ms")
    else:
        print("No successful reads.")
        
    if write_latencies:
        avg_write = sum(write_latencies) / len(write_latencies)
        max_write = max(write_latencies)
        print(f"Write Latency: Avg={avg_write*1000:.2f}ms, Max={max_write*1000:.2f}ms")
    else:
        print("No successful writes.")
        
    return lock_errors, total_reads, total_writes


def run_live_watchdog_stress(watch_dir: Path, db_path: Path):
    """
    If running in a live approved environment, this performs rapid file modifications
    in the watched directory and checks if the database updates.
    """
    print(f"\n--- Live Watchdog Stress Test on {watch_dir} ---")
    if not watch_dir.exists():
        print(f"Skipping live watchdog stress: watched directory '{watch_dir}' does not exist.")
        return False
        
    stress_subdir = watch_dir / "WatchdogStressTests"
    stress_subdir.mkdir(exist_ok=True)
    
    try:
        # 1. Create a C++ header file rapidly
        test_file = stress_subdir / "WatchdogTest.h"
        print(f"Creating test file: {test_file}")
        
        test_file.write_text("""
        class AWatchdogStressTarget {
        public:
            int InitialVariable;
        };
        """, encoding="utf-8")
        
        # Poll database to check if watchdog processed the file
        print("Polling database for Class 'AWatchdogStressTarget'...")
        found = False
        for i in range(20):
            time.sleep(0.5)
            if not db_path.exists():
                continue
            conn = sqlite3.connect(db_path)
            cursor = conn.cursor()
            cursor.execute("SELECT id FROM symbols WHERE name = 'AWatchdogStressTarget';")
            row = cursor.fetchone()
            conn.close()
            if row:
                print(f"Success! Watchdog processed file creation in {i*0.5:.1f} seconds.")
                found = True
                break
                
        if not found:
            print("Failed: Watchdog did not index AWatchdogStressTarget within 10 seconds.")
            return False
            
        # 2. Modify the file rapidly
        print("Modifying test file rapidly 10 times...")
        for i in range(10):
            test_file.write_text(f"""
            class AWatchdogStressTarget {{
            public:
                int InitialVariable;
                int ExtraVariable_{i};
            }};
            """, encoding="utf-8")
            time.sleep(FILE_OP_DELAY)
            
        # Poll database to check if it has the latest modification
        print("Checking if database holds the latest modification...")
        found_mod = False
        for i in range(20):
            time.sleep(0.5)
            conn = sqlite3.connect(db_path)
            cursor = conn.cursor()
            cursor.execute("SELECT uproperty_metadata FROM properties WHERE name = 'ExtraVariable_9';")
            # Wait, properties is stored in properties table. Let's see if we can find the property
            cursor.execute("SELECT id FROM properties WHERE name = 'ExtraVariable_9';")
            row = cursor.fetchone()
            conn.close()
            if row:
                print(f"Success! Watchdog processed updates up to ExtraVariable_9.")
                found_mod = True
                break
                
        if not found_mod:
            print("Failed: Watchdog did not catch up to the latest modification (ExtraVariable_9) within 10 seconds.")
            return False
            
        # 3. Delete the file
        print(f"Deleting test file: {test_file}")
        test_file.unlink()
        
        # Poll database to check if watchdog cleaned up the symbols
        print("Polling database to check if symbols were deleted...")
        cleaned = False
        for i in range(20):
            time.sleep(0.5)
            conn = sqlite3.connect(db_path)
            cursor = conn.cursor()
            cursor.execute("SELECT id FROM symbols WHERE name = 'AWatchdogStressTarget';")
            row = cursor.fetchone()
            conn.close()
            if not row:
                print(f"Success! Watchdog deleted symbols from database in {i*0.5:.1f} seconds.")
                cleaned = True
                break
                
        if not cleaned:
            print("Failed: Watchdog did not delete symbols from DB after file deletion.")
            return False
            
        print("Live watchdog stress test PASSED.")
        return True
    finally:
        shutil.rmtree(stress_subdir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Watchdog and Database Concurrency Stress Test")
    parser.add_argument("--watch-dir", type=str, default=str(DEFAULT_WATCH_DIR), help="Watched source directory")
    parser.add_argument("--db-path", type=str, default=str(DEFAULT_DB_PATH), help="AST cache database path")
    parser.add_argument("--simulate-only", action="store_true", help="Only run simulated database stress test")
    args = parser.parse_args()

    watch_dir = Path(args.watch_dir)
    db_path = Path(args.db_path)

    # 1. Setup isolated database schema for simulated test
    temp_dir = tempfile.mkdtemp()
    temp_db_path = Path(temp_dir) / "stress_ast_cache.db"
    
    try:
        # Initialize temp DB schema
        import UnrealEngine.ExternalServer.src.main as main_module
        main_module.DB_PATH = temp_db_path
        main_module.init_db()
        
        # Run stress test without WAL
        locks_delete, reads_delete, writes_delete = run_concurrency_stress_test(temp_db_path, use_wal=False)
        
        # Run stress test with WAL
        locks_wal, reads_wal, writes_wal = run_concurrency_stress_test(temp_db_path, use_wal=True)
        
        print("\n=== Concurrency Analysis ===")
        print(f"Without WAL (DELETE Mode): Locks={locks_delete}, Reads={reads_delete}, Writes={writes_delete}")
        print(f"With WAL Mode:            Locks={locks_wal}, Reads={reads_wal}, Writes={writes_wal}")
        
        if locks_delete > 0 and locks_wal == 0:
            print("\nCONCLUSION: WAL mode successfully eliminates database locks under concurrent read/write stress!")
        elif locks_delete > 0:
            print(f"\nCONCLUSION: DELETE mode experiences {locks_delete} lock errors. WAL mode reduced locks to {locks_wal}.")
        else:
            print("\nCONCLUSION: No lock errors encountered during this run, but WAL mode generally provides better throughput.")
            
    finally:
        try:
            shutil.rmtree(temp_dir, ignore_errors=True)
        except Exception:
            pass

    # 2. Live Watchdog test (if requested and path exists)
    if not args.simulate_only:
        run_live_watchdog_stress(watch_dir, db_path)


if __name__ == "__main__":
    main()
