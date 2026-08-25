from __future__ import annotations

from bisect import bisect_left, bisect_right
from typing import Any, Iterator, List, Optional, Tuple, Union


class LeafNode:
    __slots__ = ("keys", "values", "next")

    def __init__(self) -> None:
        self.keys: List[Any] = []
        self.values: List[Any] = []
        self.next: Optional["LeafNode"] = None


class InternalNode:
    __slots__ = ("keys", "children")

    def __init__(self) -> None:
        self.keys: List[Any] = []
        self.children: List["Node"] = []


Node = Union[LeafNode, InternalNode]


class BPlusTree:
    def __init__(self, order: int = 4) -> None:
        if order < 3:
            raise ValueError("order must be >= 3")
        self.order = order
        self.leaf_max = order - 1
        self.leaf_min = order // 2
        self.internal_max_keys = order - 1
        self.internal_min_children = (order + 1) // 2
        self.internal_min_keys = self.internal_min_children - 1
        self.root: Node = LeafNode()
        self._size = 0

    def __len__(self) -> int:
        return self._size

    def __contains__(self, key: Any) -> bool:
        _, _, found = self._locate(key)
        return found

    def __iter__(self) -> Iterator[Any]:
        for key, _ in self.items():
            yield key

    def _leftmost_leaf(self) -> LeafNode:
        node = self.root
        while isinstance(node, InternalNode):
            node = node.children[0]
        return node

    def items(self) -> Iterator[Tuple[Any, Any]]:
        leaf = self._leftmost_leaf()
        while leaf is not None:
            for key, value in zip(leaf.keys, leaf.values):
                yield key, value
            leaf = leaf.next

    def _locate(self, key: Any) -> Tuple[LeafNode, int, bool]:
        node = self.root
        while isinstance(node, InternalNode):
            idx = bisect_right(node.keys, key)
            node = node.children[idx]
        pos = bisect_left(node.keys, key)
        found = pos < len(node.keys) and node.keys[pos] == key
        return node, pos, found

    def search(self, key: Any) -> Optional[Any]:
        leaf, pos, found = self._locate(key)
        return leaf.values[pos] if found else None

    def range(self, start: Any, end: Any) -> Iterator[Tuple[Any, Any]]:
        if start > end:
            return
        node = self.root
        while isinstance(node, InternalNode):
            idx = bisect_right(node.keys, start)
            node = node.children[idx]
        leaf: Optional[LeafNode] = node
        while leaf is not None:
            for key, value in zip(leaf.keys, leaf.values):
                if key > end:
                    return
                if key >= start:
                    yield key, value
            leaf = leaf.next

    def _descend_to_leaf(self, key: Any) -> Tuple[LeafNode, List[Tuple[InternalNode, int]]]:
        ancestors: List[Tuple[InternalNode, int]] = []
        node = self.root
        while isinstance(node, InternalNode):
            idx = bisect_right(node.keys, key)
            ancestors.append((node, idx))
            node = node.children[idx]
        return node, ancestors

    def insert(self, key: Any, value: Any) -> None:
        leaf, ancestors = self._descend_to_leaf(key)
        pos = bisect_left(leaf.keys, key)
        if pos < len(leaf.keys) and leaf.keys[pos] == key:
            leaf.values[pos] = value
            return
        leaf.keys.insert(pos, key)
        leaf.values.insert(pos, value)
        self._size += 1
        if len(leaf.keys) <= self.leaf_max:
            return
        right = self._split_leaf(leaf)
        self._insert_into_parent(leaf, right.keys[0], right, ancestors)

    def _split_leaf(self, leaf: LeafNode) -> LeafNode:
        mid = len(leaf.keys) // 2
        right = LeafNode()
        right.keys = leaf.keys[mid:]
        right.values = leaf.values[mid:]
        del leaf.keys[mid:]
        del leaf.values[mid:]
        right.next = leaf.next
        leaf.next = right
        return right

    def _split_internal(self, node: InternalNode) -> Tuple[Any, InternalNode]:
        mid = len(node.keys) // 2
        promoted = node.keys[mid]
        right = InternalNode()
        right.keys = node.keys[mid + 1:]
        right.children = node.children[mid + 1:]
        del node.keys[mid:]
        del node.children[mid + 1:]
        return promoted, right

    def _insert_into_parent(
        self,
        left: Node,
        sep_key: Any,
        right: Node,
        ancestors: List[Tuple[InternalNode, int]],
    ) -> None:
        if not ancestors:
            new_root = InternalNode()
            new_root.keys = [sep_key]
            new_root.children = [left, right]
            self.root = new_root
            return
        parent, idx = ancestors[-1]
        parent.keys.insert(idx, sep_key)
        parent.children.insert(idx + 1, right)
        if len(parent.keys) <= self.internal_max_keys:
            return
        promoted, new_right = self._split_internal(parent)
        self._insert_into_parent(parent, promoted, new_right, ancestors[:-1])

    def delete(self, key: Any) -> None:
        leaf, ancestors = self._descend_to_leaf(key)
        pos = bisect_left(leaf.keys, key)
        if pos >= len(leaf.keys) or leaf.keys[pos] != key:
            raise KeyError(key)
        del leaf.keys[pos]
        del leaf.values[pos]
        self._size -= 1
        self._fix_after_delete(leaf, ancestors)

    def _fix_after_delete(self, node: Node, ancestors: List[Tuple[InternalNode, int]]) -> None:
        if not ancestors:
            if isinstance(node, InternalNode) and len(node.children) == 1:
                self.root = node.children[0]
            return
        min_keys = self.leaf_min if isinstance(node, LeafNode) else self.internal_min_keys
        if len(node.keys) >= min_keys:
            return
        parent, idx = ancestors[-1]
        left_sibling = parent.children[idx - 1] if idx > 0 else None
        right_sibling = parent.children[idx + 1] if idx + 1 < len(parent.children) else None
        if isinstance(node, LeafNode):
            self._fix_leaf(node, parent, idx, left_sibling, right_sibling, ancestors)
        else:
            self._fix_internal(node, parent, idx, left_sibling, right_sibling, ancestors)

    def _fix_leaf(
        self,
        node: LeafNode,
        parent: InternalNode,
        idx: int,
        left_sibling: Optional[LeafNode],
        right_sibling: Optional[LeafNode],
        ancestors: List[Tuple[InternalNode, int]],
    ) -> None:
        if left_sibling is not None and len(left_sibling.keys) > self.leaf_min:
            node.keys.insert(0, left_sibling.keys.pop())
            node.values.insert(0, left_sibling.values.pop())
            parent.keys[idx - 1] = node.keys[0]
            return
        if right_sibling is not None and len(right_sibling.keys) > self.leaf_min:
            node.keys.append(right_sibling.keys.pop(0))
            node.values.append(right_sibling.values.pop(0))
            parent.keys[idx] = right_sibling.keys[0]
            return
        if left_sibling is not None:
            left_sibling.keys.extend(node.keys)
            left_sibling.values.extend(node.values)
            left_sibling.next = node.next
            del parent.keys[idx - 1]
            del parent.children[idx]
            self._fix_after_delete(parent, ancestors[:-1])
            return
        assert right_sibling is not None
        node.keys.extend(right_sibling.keys)
        node.values.extend(right_sibling.values)
        node.next = right_sibling.next
        del parent.keys[idx]
        del parent.children[idx + 1]
        self._fix_after_delete(parent, ancestors[:-1])

    def _fix_internal(
        self,
        node: InternalNode,
        parent: InternalNode,
        idx: int,
        left_sibling: Optional[InternalNode],
        right_sibling: Optional[InternalNode],
        ancestors: List[Tuple[InternalNode, int]],
    ) -> None:
        if left_sibling is not None and len(left_sibling.keys) > self.internal_min_keys:
            node.keys.insert(0, parent.keys[idx - 1])
            parent.keys[idx - 1] = left_sibling.keys.pop()
            node.children.insert(0, left_sibling.children.pop())
            return
        if right_sibling is not None and len(right_sibling.keys) > self.internal_min_keys:
            node.keys.append(parent.keys[idx])
            parent.keys[idx] = right_sibling.keys.pop(0)
            node.children.append(right_sibling.children.pop(0))
            return
        if left_sibling is not None:
            left_sibling.keys.append(parent.keys[idx - 1])
            left_sibling.keys.extend(node.keys)
            left_sibling.children.extend(node.children)
            del parent.keys[idx - 1]
            del parent.children[idx]
            self._fix_after_delete(parent, ancestors[:-1])
            return
        assert right_sibling is not None
        node.keys.append(parent.keys[idx])
        node.keys.extend(right_sibling.keys)
        node.children.extend(right_sibling.children)
        del parent.keys[idx]
        del parent.children[idx + 1]
        self._fix_after_delete(parent, ancestors[:-1])

    def is_balanced(self) -> bool:
        def leaf_depth(node: Node, depth: int) -> Optional[int]:
            if isinstance(node, LeafNode):
                return depth
            depths = set()
            for child in node.children:
                d = leaf_depth(child, depth + 1)
                if d is None:
                    return None
                depths.add(d)
            return depths.pop() if len(depths) == 1 else None

        return leaf_depth(self.root, 0) is not None

    def is_valid(self) -> bool:
        if not self.is_balanced():
            return False

        def check(node: Node, is_root: bool) -> bool:
            if isinstance(node, LeafNode):
                if not is_root and len(node.keys) < self.leaf_min:
                    return False
                return list(node.keys) == sorted(node.keys) and len(set(node.keys)) == len(node.keys)
            if not is_root and len(node.children) < self.internal_min_children:
                return False
            if len(node.children) != len(node.keys) + 1:
                return False
            return all(check(child, False) for child in node.children)

        return check(self.root, True)
