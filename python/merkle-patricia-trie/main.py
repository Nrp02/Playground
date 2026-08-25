from mpt import MerklePatriciaTrie, verify


def main() -> int:
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

    print("=== initial trie ===")
    for key in entries:
        print(f"  get({key!r}) = {trie.get(key)!r}")
    root1 = trie.root_hash()
    print(f"root hash: {root1}")

    print("\n=== updating a value ===")
    trie.insert(b"apple", b"fruit-red-v2")
    root2 = trie.root_hash()
    print(f"root hash after updating 'apple': {root2}")
    assert root2 != root1

    print("\n=== deleting a key ===")
    trie.delete(b"band")
    root3 = trie.root_hash()
    print(f"root hash after deleting 'band': {root3}")
    print(f"get(b'band') after delete: {trie.get(b'band')!r}")
    assert root3 != root2

    print("\n=== merkle proof ===")
    proof = trie.get_proof(b"application")
    value = trie.get(b"application")
    ok = verify(proof, root3, b"application", value)
    print(f"proof for 'application' verifies against current root: {ok}")
    assert ok

    print("\n=== tampered verification ===")
    tampered_ok = verify(proof, root3, b"application", b"tampered-value")
    print(f"proof verification against tampered value: {tampered_ok}")
    assert not tampered_ok

    wrong_root_ok = verify(proof, "0" * 64, b"application", value)
    print(f"proof verification against wrong root hash: {wrong_root_ok}")
    assert not wrong_root_ok

    print("\n=== order independence ===")
    keys = [b"apple", b"app", b"application", b"banana"]
    trie_a = MerklePatriciaTrie()
    trie_b = MerklePatriciaTrie()
    for key in keys:
        trie_a.insert(key, entries[key])
    for key in reversed(keys):
        trie_b.insert(key, entries[key])
    print(f"root A (forward insert order): {trie_a.root_hash()}")
    print(f"root B (reverse insert order): {trie_b.root_hash()}")
    assert trie_a.root_hash() == trie_b.root_hash()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
