import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from lsm.store import LSMStore


class LSMTestCase(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="lsm-test-")

    def tearDown(self):
        shutil.rmtree(self.directory, ignore_errors=True)


class TestBasicOperations(LSMTestCase):
    def test_put_and_get(self):
        store = LSMStore(self.directory, memtable_limit_bytes=1 << 20)
        store.put("a", "1")
        store.put("b", "2")
        self.assertEqual(store.get("a"), "1")
        self.assertEqual(store.get("b"), "2")
        self.assertIsNone(store.get("missing"))

    def test_delete_creates_tombstone(self):
        store = LSMStore(self.directory, memtable_limit_bytes=1 << 20)
        store.put("a", "1")
        store.delete("a")
        self.assertFalse(store.contains("a"))
        self.assertEqual(store.get("a", "<default>"), "<default>")

    def test_overwrite_returns_latest_value(self):
        store = LSMStore(self.directory, memtable_limit_bytes=1 << 20)
        store.put("a", "1")
        store.put("a", "2")
        self.assertEqual(store.get("a"), "2")


class TestFlushAndSSTables(LSMTestCase):
    def test_small_memtable_triggers_flush(self):
        store = LSMStore(self.directory, memtable_limit_bytes=32)
        for i in range(20):
            store.put(f"k{i}", f"v{i}")
        self.assertGreater(len(store.sstables), 0)
        for i in range(20):
            self.assertEqual(store.get(f"k{i}"), f"v{i}")

    def test_newer_sstable_shadows_older_value(self):
        store = LSMStore(self.directory, memtable_limit_bytes=1)
        store.put("a", "old")
        store.put("a", "new")
        self.assertEqual(store.get("a"), "new")


class TestCompaction(LSMTestCase):
    def test_compaction_merges_into_single_sstable_and_preserves_values(self):
        store = LSMStore(self.directory, memtable_limit_bytes=16)
        for i in range(30):
            store.put(f"k{i}", f"v{i}")
        self.assertGreater(len(store.sstables), 1)

        store.compact()
        self.assertEqual(len(store.sstables), 1)
        for i in range(30):
            self.assertEqual(store.get(f"k{i}"), f"v{i}")

    def test_compaction_drops_tombstoned_keys(self):
        store = LSMStore(self.directory, memtable_limit_bytes=16)
        for i in range(10):
            store.put(f"k{i}", f"v{i}")
        store.delete("k5")
        store.compact()
        self.assertFalse(store.contains("k5"))
        for i in range(10):
            if i != 5:
                self.assertEqual(store.get(f"k{i}"), f"v{i}")


class TestRecovery(LSMTestCase):
    def test_recovers_unflushed_writes_from_wal(self):
        store = LSMStore(self.directory, memtable_limit_bytes=1 << 20)
        store.put("durable", "yes")
        store.close()

        reopened = LSMStore(self.directory, memtable_limit_bytes=1 << 20)
        self.assertEqual(reopened.get("durable"), "yes")
        reopened.close()

    def test_recovery_preserves_flushed_sstables_and_wal_tail(self):
        store = LSMStore(self.directory, memtable_limit_bytes=32)
        for i in range(10):
            store.put(f"k{i}", f"v{i}")
        store.put("tail", "value")
        store.close()

        reopened = LSMStore(self.directory, memtable_limit_bytes=32)
        for i in range(10):
            self.assertEqual(reopened.get(f"k{i}"), f"v{i}")
        self.assertEqual(reopened.get("tail"), "value")
        reopened.close()


if __name__ == "__main__":
    unittest.main()
