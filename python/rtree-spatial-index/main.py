import random
import sys

from rtree.tree import RTree


def random_bbox(rng: random.Random, span: float, size: float) -> tuple:
    x = rng.uniform(0, span)
    y = rng.uniform(0, span)
    w = rng.uniform(0.1, size)
    h = rng.uniform(0.1, size)
    return (x, y, x + w, y + h)


def brute_force_search(entries, bbox):
    from rtree.geometry import intersects

    return [eid for eid, ebbox in entries if intersects(ebbox, bbox)]


def main() -> int:
    rng = random.Random(42)
    tree = RTree(min_entries=3, max_entries=8)
    reference = {}

    n_random = 30000
    for i in range(n_random):
        bbox = random_bbox(rng, span=10000.0, size=20.0)
        tree.insert(i, bbox)
        reference[i] = bbox

    hotspots = [(500.0, 500.0), (9000.0, 9200.0), (4800.0, 200.0)]
    next_id = n_random
    for hx, hy in hotspots:
        for _ in range(500):
            x = hx + rng.uniform(-15.0, 15.0)
            y = hy + rng.uniform(-15.0, 15.0)
            w = rng.uniform(0.1, 3.0)
            h = rng.uniform(0.1, 3.0)
            bbox = (x, y, x + w, y + h)
            tree.insert(next_id, bbox)
            reference[next_id] = bbox
            next_id += 1

    print(f"inserted {len(reference)} entries")
    print(f"total nodes in tree: {tree.count_nodes()}")

    window = (450.0, 450.0, 550.0, 550.0)
    results, visited = tree.search(window)
    expected = brute_force_search(reference.items(), window)
    print(
        f"window query {window}: found {len(results)} (expected {len(expected)}), "
        f"visited {visited}/{tree.count_nodes()} nodes"
    )
    assert set(results) == set(expected)

    contained_box = (0.0, 0.0, 200.0, 200.0)
    contained, visited_c = tree.contained_in(contained_box)
    print(
        f"contained_in {contained_box}: found {len(contained)}, "
        f"visited {visited_c}/{tree.count_nodes()} nodes"
    )

    query_point = (9000.0, 9200.0)
    k = 10
    nearest, visited_k = tree.nearest(query_point, k)
    print(
        f"{k}-nearest to {query_point}: visited {visited_k}/{tree.count_nodes()} nodes"
    )
    for eid, dist_sq in nearest[:5]:
        print(f"  id={eid} dist={dist_sq ** 0.5:.3f}")

    to_delete = list(reference.keys())[: len(reference) // 3]
    for eid in to_delete:
        tree.delete(eid)
        del reference[eid]

    print(f"deleted {len(to_delete)} entries, {len(reference)} remain")
    print(f"total nodes after delete: {tree.count_nodes()}")

    results2, visited2 = tree.search(window)
    expected2 = brute_force_search(reference.items(), window)
    print(
        f"post-delete window query: found {len(results2)} (expected {len(expected2)}), "
        f"visited {visited2}/{tree.count_nodes()} nodes"
    )
    assert set(results2) == set(expected2)

    sample_ids = rng.sample(list(reference.keys()), min(500, len(reference)))
    for eid in sample_ids:
        found, _ = tree.search(reference[eid])
        assert eid in found

    print("all post-delete invariant spot-checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
