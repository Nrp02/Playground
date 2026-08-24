import shutil
import tempfile

from lsm.store import LSMStore


def main() -> int:
    directory = tempfile.mkdtemp(prefix="lsm-demo-")
    try:
        print(f"=== writing keys into {directory} (small memtable to force flushes) ===")
        store = LSMStore(directory, memtable_limit_bytes=256)
        for i in range(200):
            store.put(f"key-{i:04d}", f"value-{i}")
        print(f"sstables on disk after bulk load: {len(store.sstables)}")

        for i in range(0, 200, 40):
            store.delete(f"key-{i:04d}")

        print("get key-0000 (deleted):", store.get("key-0000", "<missing>"))
        print("get key-0001 (present):", store.get("key-0001", "<missing>"))

        print("\n=== compacting ===")
        store.compact()
        print(f"sstables on disk after compaction: {len(store.sstables)}")
        print("get key-0040 (deleted, should stay deleted):", store.get("key-0040", "<missing>"))
        print("get key-0150 (present):", store.get("key-0150", "<missing>"))

        print("\n=== simulating a crash: writing without triggering a flush ===")
        store.put("unflushed-key", "unflushed-value")
        store.close()

        print("=== reopening the store and recovering from the WAL ===")
        recovered = LSMStore(directory, memtable_limit_bytes=256)
        print("recovered unflushed-key:", recovered.get("unflushed-key", "<missing>"))
        print("recovered key-0150:", recovered.get("key-0150", "<missing>"))
        print("recovered key-0000 (deleted):", recovered.get("key-0000", "<missing>"))
        recovered.close()
        return 0
    finally:
        shutil.rmtree(directory, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
