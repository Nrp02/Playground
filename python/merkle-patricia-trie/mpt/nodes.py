from __future__ import annotations

import hashlib
from typing import List, Optional, Union


def _encode_bytes(data: bytes) -> bytes:
    return len(data).to_bytes(4, "big") + data


def _encode_nibbles(nibbles: List[int]) -> bytes:
    return _encode_bytes(bytes(nibbles))


def hash_leaf(nibbles: List[int], value: bytes) -> str:
    payload = b"L" + _encode_nibbles(nibbles) + _encode_bytes(value)
    return hashlib.sha256(payload).hexdigest()


def hash_extension(nibbles: List[int], child_hash: str) -> str:
    payload = b"E" + _encode_nibbles(nibbles) + _encode_bytes(bytes.fromhex(child_hash))
    return hashlib.sha256(payload).hexdigest()


def hash_branch(children: List[Optional[str]], value: Optional[bytes]) -> str:
    parts = [b"B"]
    for child_hash in children:
        if child_hash is None:
            parts.append(_encode_bytes(b""))
        else:
            parts.append(_encode_bytes(bytes.fromhex(child_hash)))
    if value is None:
        parts.append(b"\x00")
    else:
        parts.append(b"\x01" + _encode_bytes(value))
    return hashlib.sha256(b"".join(parts)).hexdigest()


EMPTY_TRIE_HASH = hashlib.sha256(b"EMPTY").hexdigest()


class LeafNode:
    def __init__(self, nibbles: List[int], value: bytes) -> None:
        self.nibbles = list(nibbles)
        self.value = value

    def hash(self) -> str:
        return hash_leaf(self.nibbles, self.value)


class ExtensionNode:
    def __init__(self, nibbles: List[int], child: "Node") -> None:
        self.nibbles = list(nibbles)
        self.child = child

    def hash(self) -> str:
        return hash_extension(self.nibbles, self.child.hash())


class BranchNode:
    def __init__(self) -> None:
        self.children: List[Optional["Node"]] = [None] * 16
        self.value: Optional[bytes] = None

    def hash(self) -> str:
        child_hashes = [c.hash() if c is not None else None for c in self.children]
        return hash_branch(child_hashes, self.value)


Node = Union[LeafNode, ExtensionNode, BranchNode]
