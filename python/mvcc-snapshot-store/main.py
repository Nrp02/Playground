from mvcc import MVCCStore, WriteConflictError


def main() -> int:
    store = MVCCStore()

    seed = store.begin_transaction()
    seed.put("balance:alice", 100)
    seed.put("balance:bob", 50)
    seed.commit()

    print("=== snapshot isolation: reader keeps its original snapshot ===")
    reader = store.begin_transaction()
    print(f"reader sees balance:alice = {reader.get('balance:alice')} (before any concurrent write)")

    writer = store.begin_transaction()
    writer.put("balance:alice", 999)
    writer.commit()
    print("a concurrent transaction committed balance:alice = 999")

    print(f"reader still sees balance:alice = {reader.get('balance:alice')} (unaffected, own snapshot)")
    reader.commit()

    fresh = store.begin_transaction()
    print(f"a brand new transaction sees balance:alice = {fresh.get('balance:alice')} (post-commit value)")
    fresh.commit()

    print("\n=== write-write conflict: first committer wins ===")
    txn_a = store.begin_transaction()
    txn_b = store.begin_transaction()
    txn_a.put("balance:bob", 40)
    txn_b.put("balance:bob", 30)

    txn_a.commit()
    print("txn_a committed balance:bob = 40")

    try:
        txn_b.commit()
        print("txn_b committed unexpectedly")
    except WriteConflictError as exc:
        print(f"txn_b rejected: {exc}")

    check = store.begin_transaction()
    print(f"final balance:bob = {check.get('balance:bob')}")
    check.commit()

    print("\n=== garbage collection of unreachable versions ===")
    print(f"versions of balance:alice before gc = {store.version_count('balance:alice')}")

    long_reader = store.begin_transaction()
    print(f"long_reader snapshot sees balance:alice = {long_reader.get('balance:alice')}")

    churn = store.begin_transaction()
    churn.put("balance:alice", 111)
    churn.commit()
    churn2 = store.begin_transaction()
    churn2.put("balance:alice", 222)
    churn2.commit()

    print(f"versions of balance:alice before gc (with long_reader still active) = {store.version_count('balance:alice')}")
    removed = store.garbage_collect()
    print(f"gc removed {removed} version(s) while long_reader is active (keeps the one it needs)")
    print(f"long_reader still sees balance:alice = {long_reader.get('balance:alice')}")

    long_reader.commit()
    removed = store.garbage_collect()
    print(f"gc removed {removed} more version(s) after long_reader finished")
    print(f"versions of balance:alice after final gc = {store.version_count('balance:alice')}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
