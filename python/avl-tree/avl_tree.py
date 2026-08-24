from __future__ import annotations

from typing import Any, Iterator, Optional, Tuple


class AVLNode:
    __slots__ = ("key", "value", "left", "right", "height")

    def __init__(self, key: Any, value: Any) -> None:
        self.key = key
        self.value = value
        self.left: Optional["AVLNode"] = None
        self.right: Optional["AVLNode"] = None
        self.height = 1


class AVLTree:
    def __init__(self) -> None:
        self.root: Optional[AVLNode] = None
        self._size = 0

    def __len__(self) -> int:
        return self._size

    def __contains__(self, key: Any) -> bool:
        return self._find(self.root, key) is not None

    def __iter__(self) -> Iterator[Any]:
        for key, _ in self.inorder():
            yield key

    @property
    def height(self) -> int:
        return self._height(self.root)

    @staticmethod
    def _height(node: Optional[AVLNode]) -> int:
        return node.height if node is not None else 0

    @classmethod
    def _balance_factor(cls, node: Optional[AVLNode]) -> int:
        if node is None:
            return 0
        return cls._height(node.left) - cls._height(node.right)

    @classmethod
    def _update_height(cls, node: AVLNode) -> None:
        node.height = 1 + max(cls._height(node.left), cls._height(node.right))

    @classmethod
    def _rotate_right(cls, node: AVLNode) -> AVLNode:
        pivot = node.left
        assert pivot is not None
        node.left = pivot.right
        pivot.right = node
        cls._update_height(node)
        cls._update_height(pivot)
        return pivot

    @classmethod
    def _rotate_left(cls, node: AVLNode) -> AVLNode:
        pivot = node.right
        assert pivot is not None
        node.right = pivot.left
        pivot.left = node
        cls._update_height(node)
        cls._update_height(pivot)
        return pivot

    @classmethod
    def _rebalance(cls, node: AVLNode) -> AVLNode:
        cls._update_height(node)
        balance = cls._balance_factor(node)

        if balance > 1:
            if cls._balance_factor(node.left) < 0:
                node.left = cls._rotate_left(node.left)
            return cls._rotate_right(node)

        if balance < -1:
            if cls._balance_factor(node.right) > 0:
                node.right = cls._rotate_right(node.right)
            return cls._rotate_left(node)

        return node

    def insert(self, key: Any, value: Any = None) -> None:
        self.root, inserted = self._insert(self.root, key, value)
        if inserted:
            self._size += 1

    @classmethod
    def _insert(cls, node: Optional[AVLNode], key: Any, value: Any) -> Tuple[AVLNode, bool]:
        if node is None:
            return AVLNode(key, value), True

        if key < node.key:
            node.left, inserted = cls._insert(node.left, key, value)
        elif key > node.key:
            node.right, inserted = cls._insert(node.right, key, value)
        else:
            node.value = value
            return node, False

        return cls._rebalance(node), inserted

    def delete(self, key: Any) -> bool:
        self.root, deleted = self._delete(self.root, key)
        if deleted:
            self._size -= 1
        return deleted

    @classmethod
    def _delete(cls, node: Optional[AVLNode], key: Any) -> Tuple[Optional[AVLNode], bool]:
        if node is None:
            return None, False

        if key < node.key:
            node.left, deleted = cls._delete(node.left, key)
        elif key > node.key:
            node.right, deleted = cls._delete(node.right, key)
        else:
            deleted = True
            if node.left is None:
                return node.right, True
            if node.right is None:
                return node.left, True

            successor = cls._min_node(node.right)
            node.key, node.value = successor.key, successor.value
            node.right, _ = cls._delete(node.right, successor.key)

        if node is None:
            return None, deleted
        return cls._rebalance(node), deleted

    @staticmethod
    def _min_node(node: AVLNode) -> AVLNode:
        while node.left is not None:
            node = node.left
        return node

    def _find(self, node: Optional[AVLNode], key: Any) -> Optional[AVLNode]:
        while node is not None:
            if key < node.key:
                node = node.left
            elif key > node.key:
                node = node.right
            else:
                return node
        return None

    def get(self, key: Any, default: Any = None) -> Any:
        node = self._find(self.root, key)
        return node.value if node is not None else default

    def __getitem__(self, key: Any) -> Any:
        node = self._find(self.root, key)
        if node is None:
            raise KeyError(key)
        return node.value

    def min_key(self) -> Any:
        if self.root is None:
            raise KeyError("tree is empty")
        return self._min_node(self.root).key

    def max_key(self) -> Any:
        if self.root is None:
            raise KeyError("tree is empty")
        node = self.root
        while node.right is not None:
            node = node.right
        return node.key

    def inorder(self) -> list[Tuple[Any, Any]]:
        result: list[Tuple[Any, Any]] = []
        self._inorder(self.root, result)
        return result

    @classmethod
    def _inorder(cls, node: Optional[AVLNode], out: list[Tuple[Any, Any]]) -> None:
        if node is None:
            return
        cls._inorder(node.left, out)
        out.append((node.key, node.value))
        cls._inorder(node.right, out)

    def is_balanced(self) -> bool:
        return self._check_balanced(self.root)[0]

    @classmethod
    def _check_balanced(cls, node: Optional[AVLNode]) -> Tuple[bool, int]:
        if node is None:
            return True, 0
        left_ok, left_h = cls._check_balanced(node.left)
        right_ok, right_h = cls._check_balanced(node.right)
        balanced = left_ok and right_ok and abs(left_h - right_h) <= 1
        return balanced, 1 + max(left_h, right_h)

    def is_bst(self) -> bool:
        keys = [k for k, _ in self.inorder()]
        return all(keys[i] < keys[i + 1] for i in range(len(keys) - 1))
