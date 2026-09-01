from chandy_lamport.cluster import Cluster, SnapshotResult


def print_event(now: int, node_id: int, message: str) -> None:
    print(f"  tick {now}: node {node_id} {message}")


def run_until_complete(cluster: Cluster, snapshot_id: int, max_ticks: int) -> int:
    for i in range(max_ticks):
        cluster.step()
        if cluster.snapshot_complete(snapshot_id):
            return i + 1
    raise RuntimeError("snapshot did not complete in time")


def print_snapshot(cluster: Cluster, result: SnapshotResult) -> None:
    print(f"snapshot {result.snapshot_id} assembled:")
    for node_id in sorted(result.local_states):
        print(f"  local[{node_id}] = {result.local_states[node_id]}")
    if result.channel_states:
        for src, dest in sorted(result.channel_states):
            messages = result.channel_states[(src, dest)]
            print(f"  in-flight channel {src}->{dest}: {messages} (sum={sum(messages)})")
    else:
        print("  no in-flight channel state recorded")
    print(f"  local total   = {result.local_total()}")
    print(f"  channel total = {result.channel_total()}")
    print(f"  grand total   = {result.total()} (invariant = {cluster.invariant})")
    assert result.total() == cluster.invariant


def main() -> int:
    node_ids = [1, 2, 3, 4, 5]

    print("=== single initiator snapshot with in-flight traffic ===")
    cluster = Cluster(node_ids, initial_balance=100, seed=42, min_delay=1, max_delay=4, on_event=print_event)
    cluster.run(20)

    naive_total = cluster.current_balance_total()
    in_flight = cluster.in_flight_total()
    print(f"naive sum of current balances mid-traffic = {naive_total} (invariant = {cluster.invariant})")
    print(f"tokens currently in flight on channels     = {in_flight}")
    if naive_total != cluster.invariant:
        print("  -> a naive snapshot (just reading balances) is WRONG: it silently drops in-flight tokens")

    snapshot_id = cluster.initiate_snapshot(1)
    print(f"\nnode 1 initiates snapshot {snapshot_id}")
    ticks = run_until_complete(cluster, snapshot_id, 500)
    print(f"snapshot {snapshot_id} completed after {ticks} ticks")
    result = cluster.collect_snapshot(snapshot_id)
    print_snapshot(cluster, result)

    print("\n=== concurrent initiators ===")
    cluster2 = Cluster(node_ids, initial_balance=50, seed=99, min_delay=1, max_delay=4)
    cluster2.run(15)
    sid_a = cluster2.initiate_snapshot(2)
    sid_b = cluster2.initiate_snapshot(4)
    print(f"node 2 initiates snapshot {sid_a}, node 4 initiates snapshot {sid_b} concurrently")
    for _ in range(500):
        cluster2.step()
        if cluster2.snapshot_complete(sid_a) and cluster2.snapshot_complete(sid_b):
            break
    if not (cluster2.snapshot_complete(sid_a) and cluster2.snapshot_complete(sid_b)):
        raise RuntimeError("concurrent snapshots did not complete in time")

    result_a = cluster2.collect_snapshot(sid_a)
    result_b = cluster2.collect_snapshot(sid_b)
    print_snapshot(cluster2, result_a)
    print_snapshot(cluster2, result_b)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
