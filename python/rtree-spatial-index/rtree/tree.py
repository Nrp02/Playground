import heapq
from typing import Any, Dict, List, Optional, Tuple

from rtree.geometry import (
    BBox,
    Point,
    area,
    contains,
    enlargement,
    intersects,
    min_dist_point_bbox,
    union_many,
)


class Entry:
    __slots__ = ("bbox", "child", "id")

    def __init__(self, bbox: BBox, child: Optional["Node"] = None, id: Any = None) -> None:
        self.bbox = bbox
        self.child = child
        self.id = id


class Node:
    __slots__ = ("leaf", "entries", "parent")

    def __init__(self, leaf: bool) -> None:
        self.leaf = leaf
        self.entries: List[Entry] = []
        self.parent: Optional["Node"] = None


class RTree:
    def __init__(self, min_entries: int = 2, max_entries: int = 4) -> None:
        if min_entries < 1:
            raise ValueError("min_entries must be >= 1")
        if max_entries < 2 * min_entries:
            raise ValueError("max_entries must be >= 2 * min_entries")
        self.min_entries = min_entries
        self.max_entries = max_entries
        self.root = Node(leaf=True)
        self.id_to_leaf: Dict[Any, Node] = {}
        self.size = 0

    def __len__(self) -> int:
        return self.size

    def insert(self, id: Any, bbox: BBox) -> None:
        if id in self.id_to_leaf:
            raise ValueError(f"duplicate id: {id!r}")
        self._place(id, bbox)
        self.size += 1

    def _place(self, id: Any, bbox: BBox) -> None:
        leaf = self._choose_leaf(self.root, bbox)
        leaf.entries.append(Entry(bbox, id=id))
        self.id_to_leaf[id] = leaf
        self._adjust_after_insert(leaf)

    def _choose_leaf(self, node: Node, bbox: BBox) -> Node:
        while not node.leaf:
            best_entry = None
            best_key = None
            for entry in node.entries:
                key = (enlargement(entry.bbox, bbox), area(entry.bbox))
                if best_key is None or key < best_key:
                    best_key = key
                    best_entry = entry
            node = best_entry.child
        return node

    def _mbr(self, node: Node) -> BBox:
        return union_many(tuple(e.bbox for e in node.entries))

    def _adjust_after_insert(self, node: Node) -> None:
        split_node: Optional[Node] = None
        if len(node.entries) > self.max_entries:
            node, split_node = self._split(node)
        while True:
            if node is self.root:
                if split_node is not None:
                    new_root = Node(leaf=False)
                    node.parent = new_root
                    split_node.parent = new_root
                    new_root.entries = [
                        Entry(self._mbr(node), child=node),
                        Entry(self._mbr(split_node), child=split_node),
                    ]
                    self.root = new_root
                return
            parent = node.parent
            for e in parent.entries:
                if e.child is node:
                    e.bbox = self._mbr(node)
                    break
            if split_node is not None:
                split_node.parent = parent
                parent.entries.append(Entry(self._mbr(split_node), child=split_node))
            if len(parent.entries) > self.max_entries:
                parent, split_node = self._split(parent)
            else:
                split_node = None
            node = parent

    def _pick_seeds(self, entries: List[Entry]) -> Tuple[int, int]:
        best_pair = (0, 1)
        best_waste = None
        for i in range(len(entries)):
            for j in range(i + 1, len(entries)):
                a = entries[i].bbox
                b = entries[j].bbox
                waste = area(union_many((a, b))) - area(a) - area(b)
                if best_waste is None or waste > best_waste:
                    best_waste = waste
                    best_pair = (i, j)
        return best_pair

    def _pick_next(
        self, group1: List[Entry], group2: List[Entry], remaining: List[Entry]
    ) -> Tuple[int, int]:
        mbr1 = union_many(tuple(e.bbox for e in group1))
        mbr2 = union_many(tuple(e.bbox for e in group2))
        best_idx = 0
        best_diff = None
        best_target = 1
        for idx, entry in enumerate(remaining):
            d1 = enlargement(mbr1, entry.bbox)
            d2 = enlargement(mbr2, entry.bbox)
            diff = abs(d1 - d2)
            if d1 < d2:
                target = 1
            elif d2 < d1:
                target = 2
            else:
                a1 = area(mbr1)
                a2 = area(mbr2)
                if a1 < a2:
                    target = 1
                elif a2 < a1:
                    target = 2
                else:
                    target = 1 if len(group1) <= len(group2) else 2
            if best_diff is None or diff > best_diff:
                best_diff = diff
                best_idx = idx
                best_target = target
        return best_idx, best_target

    def _split(self, node: Node) -> Tuple[Node, Node]:
        entries = node.entries
        seed1_idx, seed2_idx = self._pick_seeds(entries)
        group1 = [entries[seed1_idx]]
        group2 = [entries[seed2_idx]]
        remaining = [
            e for i, e in enumerate(entries) if i != seed1_idx and i != seed2_idx
        ]
        while remaining:
            if len(group1) + len(remaining) == self.min_entries:
                group1.extend(remaining)
                remaining = []
                break
            if len(group2) + len(remaining) == self.min_entries:
                group2.extend(remaining)
                remaining = []
                break
            idx, target = self._pick_next(group1, group2, remaining)
            entry = remaining.pop(idx)
            if target == 1:
                group1.append(entry)
            else:
                group2.append(entry)
        new_node = Node(leaf=node.leaf)
        node.entries = group1
        new_node.entries = group2
        if not node.leaf:
            for e in group1:
                e.child.parent = node
            for e in group2:
                e.child.parent = new_node
        else:
            for e in group1:
                self.id_to_leaf[e.id] = node
            for e in group2:
                self.id_to_leaf[e.id] = new_node
        return node, new_node

    def delete(self, id: Any) -> None:
        if id not in self.id_to_leaf:
            raise KeyError(f"no such id: {id!r}")
        leaf = self.id_to_leaf.pop(id)
        leaf.entries = [e for e in leaf.entries if e.id != id]
        self.size -= 1
        self._condense(leaf)

    def _collect_leaf_entries(self, node: Node) -> List[Tuple[Any, BBox]]:
        if node.leaf:
            return [(e.id, e.bbox) for e in node.entries]
        result: List[Tuple[Any, BBox]] = []
        for e in node.entries:
            result.extend(self._collect_leaf_entries(e.child))
        return result

    def _condense(self, node: Node) -> None:
        orphans: List[Tuple[Any, BBox]] = []
        while node is not self.root:
            parent = node.parent
            idx = None
            for i, e in enumerate(parent.entries):
                if e.child is node:
                    idx = i
                    break
            if len(node.entries) < self.min_entries:
                parent.entries.pop(idx)
                orphans.extend(self._collect_leaf_entries(node))
            else:
                parent.entries[idx].bbox = self._mbr(node)
            node = parent
        if not self.root.leaf:
            if len(self.root.entries) == 0:
                self.root = Node(leaf=True)
            elif len(self.root.entries) == 1:
                only = self.root.entries[0].child
                only.parent = None
                self.root = only
        for oid, obbox in orphans:
            if oid in self.id_to_leaf:
                del self.id_to_leaf[oid]
            self._place(oid, obbox)

    def search(self, bbox: BBox) -> Tuple[List[Any], int]:
        results: List[Any] = []
        visited = [0]
        self._search(self.root, bbox, results, visited)
        return results, visited[0]

    def _search(
        self, node: Node, bbox: BBox, results: List[Any], visited: List[int]
    ) -> None:
        visited[0] += 1
        for e in node.entries:
            if intersects(e.bbox, bbox):
                if node.leaf:
                    results.append(e.id)
                else:
                    self._search(e.child, bbox, results, visited)

    def contained_in(self, bbox: BBox) -> Tuple[List[Any], int]:
        results: List[Any] = []
        visited = [0]
        self._contained_in(self.root, bbox, results, visited)
        return results, visited[0]

    def _contained_in(
        self, node: Node, bbox: BBox, results: List[Any], visited: List[int]
    ) -> None:
        visited[0] += 1
        for e in node.entries:
            if intersects(e.bbox, bbox):
                if node.leaf:
                    if contains(bbox, e.bbox):
                        results.append(e.id)
                else:
                    self._contained_in(e.child, bbox, results, visited)

    def nearest(self, point: Point, k: int) -> Tuple[List[Tuple[Any, float]], int]:
        if k <= 0 or self.size == 0:
            return [], 0
        heap: List[Tuple[float, int, str, Any]] = []
        counter = 0
        heapq.heappush(heap, (0.0, counter, "node", self.root))
        results: List[Tuple[Any, float]] = []
        visited = 0
        while heap and len(results) < k:
            dist, _, kind, item = heapq.heappop(heap)
            if kind == "node":
                visited += 1
                node = item
                for e in node.entries:
                    d = min_dist_point_bbox(point, e.bbox)
                    counter += 1
                    if node.leaf:
                        heapq.heappush(heap, (d, counter, "leaf", e.id))
                    else:
                        heapq.heappush(heap, (d, counter, "node", e.child))
            else:
                results.append((item, dist))
        return results, visited

    def count_nodes(self) -> int:
        return self._count_nodes(self.root)

    def _count_nodes(self, node: Node) -> int:
        total = 1
        if not node.leaf:
            for e in node.entries:
                total += self._count_nodes(e.child)
        return total
