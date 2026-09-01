import os
import random
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from rtree.geometry import contains, intersects, min_dist_point_bbox, union_many
from rtree.tree import RTree


def brute_search(entries, bbox):
    return sorted(eid for eid, ebbox in entries.items() if intersects(ebbox, bbox))


def brute_contained_in(entries, bbox):
    return sorted(eid for eid, ebbox in entries.items() if contains(bbox, ebbox))


def brute_nearest(entries, point, k):
    scored = sorted(
        ((min_dist_point_bbox(point, ebbox), eid) for eid, ebbox in entries.items())
    )
    return scored[:k]


def random_bbox(rng, span=1000.0, size=10.0):
    x = rng.uniform(0, span)
    y = rng.uniform(0, span)
    w = rng.uniform(0.1, size)
    h = rng.uniform(0.1, size)
    return (x, y, x + w, y + h)


def check_invariants(test, tree):
    root = tree.root

    def depth_of_leftmost(node):
        d = 0
        while not node.leaf:
            node = node.entries[0].child
            d += 1
        return d

    expected_depth = depth_of_leftmost(root)

    def walk(node, depth, is_root):
        if not is_root:
            test.assertGreaterEqual(len(node.entries), tree.min_entries)
        test.assertLessEqual(len(node.entries), tree.max_entries)
        if node.leaf:
            test.assertEqual(depth, expected_depth)
            return
        for e in node.entries:
            test.assertIs(e.child.parent, node)
            child_mbr = union_many(tuple(ce.bbox for ce in e.child.entries))
            test.assertEqual(child_mbr, e.bbox)
            walk(e.child, depth + 1, False)

    if tree.size > 0:
        walk(root, 0, True)


class TestGeometry(unittest.TestCase):
    def test_intersects_and_contains(self):
        a = (0.0, 0.0, 10.0, 10.0)
        b = (5.0, 5.0, 15.0, 15.0)
        c = (20.0, 20.0, 30.0, 30.0)
        self.assertTrue(intersects(a, b))
        self.assertFalse(intersects(a, c))
        self.assertTrue(contains(a, (1.0, 1.0, 2.0, 2.0)))
        self.assertFalse(contains(a, b))

    def test_min_dist_point_bbox(self):
        b = (0.0, 0.0, 10.0, 10.0)
        self.assertEqual(min_dist_point_bbox((5.0, 5.0), b), 0.0)
        self.assertEqual(min_dist_point_bbox((15.0, 0.0), b), 25.0)
        self.assertEqual(min_dist_point_bbox((-3.0, -4.0), b), 25.0)


class TestDegenerateCases(unittest.TestCase):
    def test_empty_tree(self):
        tree = RTree()
        results, visited = tree.search((0, 0, 100, 100))
        self.assertEqual(results, [])
        self.assertEqual(visited, 1)
        self.assertEqual(tree.nearest((0, 0), 5), ([], 0))

    def test_single_entry(self):
        tree = RTree()
        tree.insert("a", (0.0, 0.0, 1.0, 1.0))
        results, _ = tree.search((0.0, 0.0, 1.0, 1.0))
        self.assertEqual(results, ["a"])
        check_invariants(self, tree)
        tree.delete("a")
        self.assertEqual(tree.size, 0)
        results, _ = tree.search((0.0, 0.0, 1.0, 1.0))
        self.assertEqual(results, [])

    def test_zero_area_boxes(self):
        tree = RTree()
        tree.insert(1, (5.0, 5.0, 5.0, 5.0))
        results, _ = tree.search((0.0, 0.0, 10.0, 10.0))
        self.assertEqual(results, [1])
        results2, _ = tree.contained_in((0.0, 0.0, 10.0, 10.0))
        self.assertEqual(results2, [1])

    def test_identical_boxes(self):
        tree = RTree()
        box = (1.0, 1.0, 2.0, 2.0)
        for i in range(10):
            tree.insert(i, box)
        results, _ = tree.search(box)
        self.assertEqual(sorted(results), list(range(10)))
        check_invariants(self, tree)

    def test_duplicate_insert_id_raises(self):
        tree = RTree()
        tree.insert(1, (0.0, 0.0, 1.0, 1.0))
        with self.assertRaises(ValueError):
            tree.insert(1, (2.0, 2.0, 3.0, 3.0))

    def test_delete_missing_id_raises(self):
        tree = RTree()
        with self.assertRaises(KeyError):
            tree.delete("nope")


class TestInsertAndSearch(unittest.TestCase):
    def test_randomized_search_matches_brute_force(self):
        for seed in range(10):
            rng = random.Random(seed)
            tree = RTree(min_entries=2, max_entries=5)
            entries = {}
            for i in range(300):
                bbox = random_bbox(rng)
                tree.insert(i, bbox)
                entries[i] = bbox
            check_invariants(self, tree)
            for _ in range(20):
                query = random_bbox(rng, span=1000.0, size=50.0)
                got, visited = tree.search(query)
                expected = brute_search(entries, query)
                self.assertEqual(sorted(got), expected)
                self.assertLessEqual(visited, tree.count_nodes())

    def test_contained_in_matches_brute_force(self):
        rng = random.Random(99)
        tree = RTree(min_entries=2, max_entries=4)
        entries = {}
        for i in range(200):
            bbox = random_bbox(rng, span=500.0, size=5.0)
            tree.insert(i, bbox)
            entries[i] = bbox
        for _ in range(15):
            query = random_bbox(rng, span=500.0, size=100.0)
            got, _ = tree.contained_in(query)
            expected = brute_contained_in(entries, query)
            self.assertEqual(sorted(got), expected)

    def test_pruning_visits_fewer_than_all_nodes(self):
        rng = random.Random(5)
        tree = RTree(min_entries=3, max_entries=8)
        for i in range(2000):
            tree.insert(i, random_bbox(rng, span=5000.0, size=5.0))
        total = tree.count_nodes()
        _, visited = tree.search((0.0, 0.0, 20.0, 20.0))
        self.assertLess(visited, total)


class TestNearest(unittest.TestCase):
    def test_knn_matches_brute_force(self):
        for seed in range(10):
            rng = random.Random(1000 + seed)
            tree = RTree(min_entries=2, max_entries=5)
            entries = {}
            for i in range(250):
                bbox = random_bbox(rng, span=300.0, size=8.0)
                tree.insert(i, bbox)
                entries[i] = bbox
            for _ in range(10):
                point = (rng.uniform(0, 300), rng.uniform(0, 300))
                k = rng.randint(1, 8)
                got, visited = tree.nearest(point, k)
                expected = brute_nearest(entries, point, k)
                got_dists = sorted(d for _, d in got)
                expected_dists = sorted(d for d, _ in expected)
                self.assertEqual(len(got), len(expected))
                for gd, ed in zip(got_dists, expected_dists):
                    self.assertAlmostEqual(gd, ed, places=6)
                self.assertLessEqual(visited, tree.count_nodes())

    def test_knn_tie_handling(self):
        tree = RTree()
        tree.insert("a", (0.0, 0.0, 1.0, 1.0))
        tree.insert("b", (10.0, 0.0, 11.0, 1.0))
        tree.insert("c", (-10.0, 0.0, -9.0, 1.0))
        tree.insert("d", (0.0, 10.0, 1.0, 11.0))
        results, _ = tree.nearest((0.5, 0.5), 4)
        self.assertEqual(results[0][0], "a")
        self.assertAlmostEqual(results[0][1], 0.0)
        rest = sorted(eid for eid, _ in results[1:])
        self.assertEqual(rest, ["b", "c", "d"])

    def test_knn_k_larger_than_size(self):
        tree = RTree()
        tree.insert(1, (0.0, 0.0, 1.0, 1.0))
        tree.insert(2, (5.0, 5.0, 6.0, 6.0))
        results, _ = tree.nearest((0.0, 0.0), 10)
        self.assertEqual(len(results), 2)


class TestDelete(unittest.TestCase):
    def test_delete_preserves_invariants_and_correctness(self):
        rng = random.Random(7)
        tree = RTree(min_entries=2, max_entries=5)
        entries = {}
        for i in range(500):
            bbox = random_bbox(rng, span=1000.0, size=10.0)
            tree.insert(i, bbox)
            entries[i] = bbox
        check_invariants(self, tree)

        ids_to_delete = list(entries.keys())
        rng.shuffle(ids_to_delete)
        ids_to_delete = ids_to_delete[: len(ids_to_delete) * 2 // 3]
        for eid in ids_to_delete:
            tree.delete(eid)
            del entries[eid]
            self.assertNotIn(eid, tree.id_to_leaf)

        check_invariants(self, tree)
        self.assertEqual(tree.size, len(entries))

        for eid, bbox in entries.items():
            found, _ = tree.search(bbox)
            self.assertIn(eid, found)

        query = (0.0, 0.0, 1000.0, 1000.0)
        got, _ = tree.search(query)
        expected = brute_search(entries, query)
        self.assertEqual(sorted(got), expected)

    def test_delete_all_leaves_empty_tree(self):
        rng = random.Random(3)
        tree = RTree(min_entries=2, max_entries=4)
        ids = []
        for i in range(50):
            tree.insert(i, random_bbox(rng, span=100.0, size=5.0))
            ids.append(i)
        rng.shuffle(ids)
        for eid in ids:
            tree.delete(eid)
        self.assertEqual(tree.size, 0)
        self.assertTrue(tree.root.leaf)
        self.assertEqual(len(tree.root.entries), 0)

    def test_delete_triggers_underflow_reinsertion(self):
        tree = RTree(min_entries=2, max_entries=4)
        rng = random.Random(11)
        entries = {}
        for i in range(60):
            bbox = random_bbox(rng, span=200.0, size=5.0)
            tree.insert(i, bbox)
            entries[i] = bbox
        nodes_before = tree.count_nodes()
        for i in range(40):
            tree.delete(i)
            del entries[i]
        check_invariants(self, tree)
        for eid, bbox in entries.items():
            found, _ = tree.search(bbox)
            self.assertIn(eid, found)
        self.assertLessEqual(tree.count_nodes(), nodes_before)


if __name__ == "__main__":
    unittest.main()
