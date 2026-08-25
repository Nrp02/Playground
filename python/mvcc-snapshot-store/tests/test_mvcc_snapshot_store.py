import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mvcc import MVCCStore, TransactionClosedError, TransactionStatus, WriteConflictError


class TestSnapshotIsolation(unittest.TestCase):
    def test_reader_unaffected_by_concurrent_committed_write(self):
        store = MVCCStore()
        seed = store.begin_transaction()
        seed.put("k", 1)
        seed.commit()

        reader = store.begin_transaction()
        self.assertEqual(reader.get("k"), 1)

        writer = store.begin_transaction()
        writer.put("k", 2)
        writer.commit()

        self.assertEqual(reader.get("k"), 1)
        reader.commit()

        fresh = store.begin_transaction()
        self.assertEqual(fresh.get("k"), 2)
        fresh.commit()

    def test_transaction_sees_its_own_uncommitted_writes(self):
        store = MVCCStore()
        txn = store.begin_transaction()
        txn.put("k", "v1")
        self.assertEqual(txn.get("k"), "v1")
        txn.put("k", "v2")
        self.assertEqual(txn.get("k"), "v2")
        txn.commit()

    def test_missing_key_reads_as_none(self):
        store = MVCCStore()
        txn = store.begin_transaction()
        self.assertIsNone(txn.get("nope"))
        txn.commit()

    def test_delete_hides_key_after_commit(self):
        store = MVCCStore()
        seed = store.begin_transaction()
        seed.put("k", 1)
        seed.commit()

        deleter = store.begin_transaction()
        deleter.delete("k")
        deleter.commit()

        reader = store.begin_transaction()
        self.assertIsNone(reader.get("k"))
        reader.commit()


class TestWriteWriteConflict(unittest.TestCase):
    def test_second_committer_of_concurrent_conflicting_write_is_rejected(self):
        store = MVCCStore()
        seed = store.begin_transaction()
        seed.put("k", 0)
        seed.commit()

        txn_a = store.begin_transaction()
        txn_b = store.begin_transaction()
        txn_a.put("k", "a")
        txn_b.put("k", "b")

        txn_a.commit()
        with self.assertRaises(WriteConflictError):
            txn_b.commit()

        self.assertEqual(txn_a.status, TransactionStatus.COMMITTED)
        self.assertEqual(txn_b.status, TransactionStatus.ABORTED)

        check = store.begin_transaction()
        self.assertEqual(check.get("k"), "a")
        check.commit()

    def test_disjoint_writes_do_not_conflict(self):
        store = MVCCStore()
        txn_a = store.begin_transaction()
        txn_b = store.begin_transaction()
        txn_a.put("k1", 1)
        txn_b.put("k2", 2)
        txn_a.commit()
        txn_b.commit()

        check = store.begin_transaction()
        self.assertEqual(check.get("k1"), 1)
        self.assertEqual(check.get("k2"), 2)
        check.commit()

    def test_sequential_non_overlapping_writes_do_not_conflict(self):
        store = MVCCStore()
        txn_a = store.begin_transaction()
        txn_a.put("k", "a")
        txn_a.commit()

        txn_b = store.begin_transaction()
        txn_b.put("k", "b")
        txn_b.commit()

        check = store.begin_transaction()
        self.assertEqual(check.get("k"), "b")
        check.commit()


class TestCommitAbort(unittest.TestCase):
    def test_abort_discards_uncommitted_writes(self):
        store = MVCCStore()
        seed = store.begin_transaction()
        seed.put("k", 1)
        seed.commit()

        txn = store.begin_transaction()
        txn.put("k", 999)
        txn.abort()
        self.assertEqual(txn.status, TransactionStatus.ABORTED)

        check = store.begin_transaction()
        self.assertEqual(check.get("k"), 1)
        check.commit()

    def test_operations_after_commit_raise(self):
        store = MVCCStore()
        txn = store.begin_transaction()
        txn.put("k", 1)
        txn.commit()
        with self.assertRaises(TransactionClosedError):
            txn.put("k", 2)
        with self.assertRaises(TransactionClosedError):
            txn.get("k")
        with self.assertRaises(TransactionClosedError):
            txn.commit()

    def test_operations_after_abort_raise(self):
        store = MVCCStore()
        txn = store.begin_transaction()
        txn.abort()
        with self.assertRaises(TransactionClosedError):
            txn.get("k")
        with self.assertRaises(TransactionClosedError):
            txn.abort()


class TestGarbageCollection(unittest.TestCase):
    def test_does_not_remove_versions_visible_to_an_active_transaction(self):
        store = MVCCStore()
        seed = store.begin_transaction()
        seed.put("k", "v1")
        seed.commit()

        long_reader = store.begin_transaction()
        self.assertEqual(long_reader.get("k"), "v1")

        for value in ("v2", "v3", "v4"):
            txn = store.begin_transaction()
            txn.put("k", value)
            txn.commit()

        store.garbage_collect()
        self.assertEqual(long_reader.get("k"), "v1")
        long_reader.commit()

    def test_removes_versions_unreachable_by_any_active_transaction(self):
        store = MVCCStore()
        for value in ("v1", "v2", "v3"):
            txn = store.begin_transaction()
            txn.put("k", value)
            txn.commit()

        self.assertEqual(store.version_count("k"), 3)
        store.garbage_collect()
        self.assertEqual(store.version_count("k"), 1)

        check = store.begin_transaction()
        self.assertEqual(check.get("k"), "v3")
        check.commit()

    def test_keeps_exactly_the_versions_each_active_snapshot_still_needs(self):
        store = MVCCStore()
        seed = store.begin_transaction()
        seed.put("k", "v1")
        seed.commit()

        reader_old = store.begin_transaction()

        bump = store.begin_transaction()
        bump.put("k", "v2")
        bump.commit()

        reader_mid = store.begin_transaction()

        bump2 = store.begin_transaction()
        bump2.put("k", "v3")
        bump2.commit()

        store.garbage_collect()

        self.assertEqual(reader_old.get("k"), "v1")
        self.assertEqual(reader_mid.get("k"), "v2")

        reader_old.commit()
        reader_mid.commit()

        store.garbage_collect()
        self.assertEqual(store.version_count("k"), 1)

        check = store.begin_transaction()
        self.assertEqual(check.get("k"), "v3")
        check.commit()


if __name__ == "__main__":
    unittest.main()
