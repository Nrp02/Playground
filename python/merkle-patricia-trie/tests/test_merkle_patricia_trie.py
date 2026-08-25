import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mpt import EMPTY_TRIE_HASH, MerklePatriciaTrie, verify


class TestBasicOperations(unittest.TestCase):
    def test_empty_trie_root_hash(self):
        trie = MerklePatriciaTrie()
        self.assertEqual(trie.root_hash(), EMPTY_TRIE_HASH)

    def test_insert_and_get(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        trie.insert(b"app", b"short")
        trie.insert(b"application", b"software")
        self.assertEqual(trie.get(b"apple"), b"fruit")
        self.assertEqual(trie.get(b"app"), b"short")
        self.assertEqual(trie.get(b"application"), b"software")

    def test_get_missing_key_returns_none(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        self.assertIsNone(trie.get(b"missing"))
        self.assertIsNone(trie.get(b""))

    def test_update_existing_key(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"v1")
        trie.insert(b"apple", b"v2")
        self.assertEqual(trie.get(b"apple"), b"v2")

    def test_delete_existing_key(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        trie.insert(b"app", b"short")
        self.assertTrue(trie.delete(b"apple"))
        self.assertIsNone(trie.get(b"apple"))
        self.assertEqual(trie.get(b"app"), b"short")

    def test_delete_missing_key_returns_false(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        self.assertFalse(trie.delete(b"missing"))

    def test_delete_all_keys_yields_empty_trie(self):
        trie = MerklePatriciaTrie()
        keys = [b"apple", b"app", b"application", b"banana", b"band"]
        for key in keys:
            trie.insert(key, key.upper())
        for key in keys:
            self.assertTrue(trie.delete(key))
        self.assertEqual(trie.root_hash(), EMPTY_TRIE_HASH)
        self.assertIsNone(trie.root)

    def test_empty_key_supported(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"", b"root-value")
        trie.insert(b"a", b"a-value")
        self.assertEqual(trie.get(b""), b"root-value")
        self.assertEqual(trie.get(b"a"), b"a-value")
        self.assertTrue(trie.delete(b""))
        self.assertIsNone(trie.get(b""))
        self.assertEqual(trie.get(b"a"), b"a-value")


class TestRootHashBehavior(unittest.TestCase):
    def test_root_hash_changes_on_insert(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        h1 = trie.root_hash()
        trie.insert(b"banana", b"yellow")
        h2 = trie.root_hash()
        self.assertNotEqual(h1, h2)

    def test_root_hash_changes_on_update(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        h1 = trie.root_hash()
        trie.insert(b"apple", b"fruit-v2")
        h2 = trie.root_hash()
        self.assertNotEqual(h1, h2)

    def test_root_hash_changes_on_delete(self):
        trie = MerklePatriciaTrie()
        trie.insert(b"apple", b"fruit")
        trie.insert(b"banana", b"yellow")
        h1 = trie.root_hash()
        trie.delete(b"banana")
        h2 = trie.root_hash()
        self.assertNotEqual(h1, h2)

    def test_root_hash_deterministic_across_insert_orders(self):
        keys = [b"apple", b"app", b"application", b"banana", b"band", b"bandana"]
        values = {k: k[::-1] for k in keys}

        trie_a = MerklePatriciaTrie()
        for key in keys:
            trie_a.insert(key, values[key])

        shuffled = list(keys)
        random.Random(42).shuffle(shuffled)
        trie_b = MerklePatriciaTrie()
        for key in shuffled:
            trie_b.insert(key, values[key])

        self.assertEqual(trie_a.root_hash(), trie_b.root_hash())

    def test_root_hash_same_regardless_of_deletion_history(self):
        keys = [b"apple", b"app", b"application", b"banana", b"band"]
        values = {k: k[::-1] for k in keys}

        direct = MerklePatriciaTrie()
        for key in [b"apple", b"application"]:
            direct.insert(key, values[key])

        via_deletion = MerklePatriciaTrie()
        for key in keys:
            via_deletion.insert(key, values[key])
        for key in [b"app", b"banana", b"band"]:
            via_deletion.delete(key)

        self.assertEqual(direct.root_hash(), via_deletion.root_hash())

    def test_reinserting_deleted_key_matches_never_deleted(self):
        keys = [b"apple", b"app", b"application"]
        values = {k: k[::-1] for k in keys}

        never_deleted = MerklePatriciaTrie()
        for key in keys:
            never_deleted.insert(key, values[key])

        deleted_and_reinserted = MerklePatriciaTrie()
        for key in keys:
            deleted_and_reinserted.insert(key, values[key])
        deleted_and_reinserted.delete(b"app")
        deleted_and_reinserted.insert(b"app", values[b"app"])

        self.assertEqual(never_deleted.root_hash(), deleted_and_reinserted.root_hash())


class TestMerkleProofs(unittest.TestCase):
    def _build_trie(self):
        trie = MerklePatriciaTrie()
        entries = {
            b"apple": b"fruit-red",
            b"application": b"software",
            b"app": b"short-form",
            b"banana": b"fruit-yellow",
            b"band": b"music-group",
        }
        for key, value in entries.items():
            trie.insert(key, value)
        return trie, entries

    def test_proof_verifies_for_every_key(self):
        trie, entries = self._build_trie()
        root = trie.root_hash()
        for key, value in entries.items():
            proof = trie.get_proof(key)
            self.assertIsNotNone(proof)
            self.assertTrue(verify(proof, root, key, value))

    def test_proof_rejects_tampered_value(self):
        trie, entries = self._build_trie()
        root = trie.root_hash()
        proof = trie.get_proof(b"apple")
        self.assertFalse(verify(proof, root, b"apple", b"not-the-real-value"))

    def test_proof_rejects_wrong_root_hash(self):
        trie, entries = self._build_trie()
        proof = trie.get_proof(b"apple")
        wrong_root = "0" * 64
        self.assertFalse(verify(proof, wrong_root, b"apple", entries[b"apple"]))

    def test_proof_rejects_wrong_key(self):
        trie, entries = self._build_trie()
        root = trie.root_hash()
        proof = trie.get_proof(b"apple")
        self.assertFalse(verify(proof, root, b"banana", entries[b"apple"]))

    def test_proof_for_missing_key_is_none(self):
        trie, _ = self._build_trie()
        self.assertIsNone(trie.get_proof(b"grapefruit"))

    def test_proof_reflects_post_delete_state(self):
        trie, entries = self._build_trie()
        trie.delete(b"band")
        root = trie.root_hash()
        proof = trie.get_proof(b"apple")
        self.assertTrue(verify(proof, root, b"apple", entries[b"apple"]))
        self.assertIsNone(trie.get_proof(b"band"))

    def test_old_proof_fails_after_mutation(self):
        trie, entries = self._build_trie()
        old_root = trie.root_hash()
        old_proof = trie.get_proof(b"apple")
        trie.insert(b"apple", b"different-value")
        new_root = trie.root_hash()
        self.assertFalse(verify(old_proof, new_root, b"apple", entries[b"apple"]))
        self.assertTrue(verify(old_proof, old_root, b"apple", entries[b"apple"]))


if __name__ == "__main__":
    unittest.main()
