from __future__ import annotations

from typing import List, Optional, Tuple

from .nibbles import common_prefix_length, to_nibbles
from .nodes import EMPTY_TRIE_HASH, BranchNode, ExtensionNode, LeafNode, Node
from .proof import ProofStep


class MerklePatriciaTrie:
    def __init__(self) -> None:
        self.root: Optional[Node] = None

    def root_hash(self) -> str:
        return self.root.hash() if self.root is not None else EMPTY_TRIE_HASH

    def insert(self, key: bytes, value: bytes) -> None:
        nibbles = to_nibbles(key)
        self.root = self._insert(self.root, nibbles, value)

    def get(self, key: bytes) -> Optional[bytes]:
        nibbles = to_nibbles(key)
        return self._get(self.root, nibbles)

    def delete(self, key: bytes) -> bool:
        nibbles = to_nibbles(key)
        new_root, deleted = self._delete(self.root, nibbles)
        self.root = new_root
        return deleted

    def get_proof(self, key: bytes) -> Optional[List[ProofStep]]:
        nibbles = to_nibbles(key)
        proof: List[ProofStep] = []
        node = self.root
        remaining = nibbles
        while True:
            if node is None:
                return None
            if isinstance(node, LeafNode):
                if remaining != node.nibbles:
                    return None
                proof.append(ProofStep("leaf", list(node.nibbles), node.value, None, None, None))
                return proof
            if isinstance(node, ExtensionNode):
                cp = common_prefix_length(node.nibbles, remaining)
                if cp != len(node.nibbles):
                    return None
                proof.append(ProofStep("extension", list(node.nibbles), None, None, None, node.child.hash()))
                node = node.child
                remaining = remaining[cp:]
                continue
            if isinstance(node, BranchNode):
                child_hashes = [c.hash() if c is not None else None for c in node.children]
                if len(remaining) == 0:
                    if node.value is None:
                        return None
                    proof.append(ProofStep("branch", None, node.value, child_hashes, None, None))
                    return proof
                idx = remaining[0]
                proof.append(ProofStep("branch", None, node.value, child_hashes, idx, None))
                node = node.children[idx]
                remaining = remaining[1:]
                continue
            raise TypeError(f"unknown node type: {type(node)!r}")

    def _insert(self, node: Optional[Node], nibbles: List[int], value: bytes) -> Node:
        if node is None:
            return LeafNode(nibbles, value)
        if isinstance(node, LeafNode):
            return self._insert_leaf(node, nibbles, value)
        if isinstance(node, ExtensionNode):
            return self._insert_extension(node, nibbles, value)
        if isinstance(node, BranchNode):
            return self._insert_branch(node, nibbles, value)
        raise TypeError(f"unknown node type: {type(node)!r}")

    def _insert_leaf(self, node: LeafNode, nibbles: List[int], value: bytes) -> Node:
        if nibbles == node.nibbles:
            node.value = value
            return node
        cp = common_prefix_length(node.nibbles, nibbles)
        branch = BranchNode()
        if cp == len(node.nibbles):
            branch.value = node.value
        else:
            branch.children[node.nibbles[cp]] = LeafNode(node.nibbles[cp + 1:], node.value)
        if cp == len(nibbles):
            branch.value = value
        else:
            branch.children[nibbles[cp]] = LeafNode(nibbles[cp + 1:], value)
        if cp > 0:
            return ExtensionNode(nibbles[:cp], branch)
        return branch

    def _insert_extension(self, node: ExtensionNode, nibbles: List[int], value: bytes) -> Node:
        cp = common_prefix_length(node.nibbles, nibbles)
        if cp == len(node.nibbles):
            node.child = self._insert(node.child, nibbles[cp:], value)
            return node
        branch = BranchNode()
        remaining_old = node.nibbles[cp + 1:]
        if remaining_old:
            branch.children[node.nibbles[cp]] = ExtensionNode(remaining_old, node.child)
        else:
            branch.children[node.nibbles[cp]] = node.child
        if cp == len(nibbles):
            branch.value = value
        else:
            branch.children[nibbles[cp]] = LeafNode(nibbles[cp + 1:], value)
        if cp > 0:
            return ExtensionNode(nibbles[:cp], branch)
        return branch

    def _insert_branch(self, node: BranchNode, nibbles: List[int], value: bytes) -> Node:
        if len(nibbles) == 0:
            node.value = value
            return node
        idx = nibbles[0]
        node.children[idx] = self._insert(node.children[idx], nibbles[1:], value)
        return node

    def _get(self, node: Optional[Node], nibbles: List[int]) -> Optional[bytes]:
        if node is None:
            return None
        if isinstance(node, LeafNode):
            return node.value if nibbles == node.nibbles else None
        if isinstance(node, ExtensionNode):
            cp = common_prefix_length(node.nibbles, nibbles)
            if cp != len(node.nibbles):
                return None
            return self._get(node.child, nibbles[cp:])
        if isinstance(node, BranchNode):
            if len(nibbles) == 0:
                return node.value
            idx = nibbles[0]
            return self._get(node.children[idx], nibbles[1:])
        raise TypeError(f"unknown node type: {type(node)!r}")

    def _delete(self, node: Optional[Node], nibbles: List[int]) -> Tuple[Optional[Node], bool]:
        if node is None:
            return None, False
        if isinstance(node, LeafNode):
            if nibbles == node.nibbles:
                return None, True
            return node, False
        if isinstance(node, ExtensionNode):
            cp = common_prefix_length(node.nibbles, nibbles)
            if cp != len(node.nibbles):
                return node, False
            new_child, deleted = self._delete(node.child, nibbles[cp:])
            if not deleted:
                return node, False
            if new_child is None:
                return None, True
            return self._merge_extension(node.nibbles, new_child), True
        if isinstance(node, BranchNode):
            if len(nibbles) == 0:
                if node.value is None:
                    return node, False
                node.value = None
                return self._collapse_branch(node), True
            idx = nibbles[0]
            new_child, deleted = self._delete(node.children[idx], nibbles[1:])
            if not deleted:
                return node, False
            node.children[idx] = new_child
            return self._collapse_branch(node), True
        raise TypeError(f"unknown node type: {type(node)!r}")

    def _merge_extension(self, prefix: List[int], child: Node) -> Node:
        if isinstance(child, LeafNode):
            return LeafNode(prefix + child.nibbles, child.value)
        if isinstance(child, ExtensionNode):
            return ExtensionNode(prefix + child.nibbles, child.child)
        return ExtensionNode(prefix, child)

    def _collapse_branch(self, branch: BranchNode) -> Optional[Node]:
        present = [(i, c) for i, c in enumerate(branch.children) if c is not None]
        if len(present) == 0:
            if branch.value is None:
                return None
            return LeafNode([], branch.value)
        if len(present) == 1 and branch.value is None:
            idx, child = present[0]
            if isinstance(child, LeafNode):
                return LeafNode([idx] + child.nibbles, child.value)
            if isinstance(child, ExtensionNode):
                return ExtensionNode([idx] + child.nibbles, child.child)
            return ExtensionNode([idx], child)
        return branch
