from gossip.cluster import Cluster
from gossip.node import MemberState


def describe(cluster: Cluster) -> None:
    for observer_id in sorted(cluster.nodes):
        row = []
        for target_id in sorted(cluster.nodes):
            if target_id == observer_id:
                continue
            row.append(f"{target_id}:{cluster.state_of(observer_id, target_id).name}")
        tag = " (silenced)" if observer_id in cluster.silenced else ""
        print(f"  node {observer_id}{tag}: {' '.join(row)}")


def wait_until(cluster: Cluster, max_ticks: int, predicate) -> int:
    for i in range(max_ticks):
        cluster.step()
        if predicate():
            return i + 1
    raise RuntimeError("condition not reached in time")


def main() -> int:
    node_ids = [1, 2, 3, 4, 5]
    cluster = Cluster(node_ids, seed=11)

    print("=== initial convergence ===")
    ticks = wait_until(cluster, 2000, cluster.fully_converged)
    print(f"cluster converged after {ticks} ticks")
    describe(cluster)

    print("\n=== node 5 goes silent ===")
    cluster.silence(5)
    ticks = wait_until(cluster, 2000, lambda: cluster.all_agree(5, MemberState.FAILED))
    print(f"all surviving nodes marked node 5 FAILED after {ticks} ticks")
    describe(cluster)

    print("\n=== node 5 rejoins ===")
    cluster.rejoin(5)
    ticks = wait_until(
        cluster,
        2000,
        lambda: all(cluster.state_of(n, 5) == MemberState.ALIVE for n in node_ids if n != 5),
    )
    print(f"all nodes marked node 5 ALIVE again after {ticks} ticks")
    describe(cluster)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
