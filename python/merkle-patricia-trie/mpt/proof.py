from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional

from .nibbles import to_nibbles
from .nodes import hash_branch, hash_extension, hash_leaf


@dataclass
class ProofStep:
    kind: str
    nibbles: Optional[List[int]]
    value: Optional[bytes]
    children: Optional[List[Optional[str]]]
    nibble_index: Optional[int]
    child_hash: Optional[str]


def _step_hash(step: ProofStep) -> str:
    if step.kind == "leaf":
        return hash_leaf(step.nibbles, step.value)
    if step.kind == "extension":
        return hash_extension(step.nibbles, step.child_hash)
    if step.kind == "branch":
        return hash_branch(step.children, step.value)
    raise ValueError(f"unknown proof step kind: {step.kind}")


def verify(proof: List[ProofStep], root_hash: str, key: bytes, value: bytes) -> bool:
    if not proof:
        return False
    nibbles = to_nibbles(key)
    consumed: List[int] = []
    for i, step in enumerate(proof):
        step_hash = _step_hash(step)
        if i == 0:
            if step_hash != root_hash:
                return False
        else:
            parent = proof[i - 1]
            if parent.kind == "extension":
                if parent.nibbles is None or parent.child_hash != step_hash:
                    return False
                consumed.extend(parent.nibbles)
            elif parent.kind == "branch":
                if parent.nibble_index is None or parent.children is None:
                    return False
                if parent.children[parent.nibble_index] != step_hash:
                    return False
                consumed.append(parent.nibble_index)
            else:
                return False

    last = proof[-1]
    if last.kind == "leaf":
        if last.nibbles is None or last.value != value:
            return False
        consumed.extend(last.nibbles)
    elif last.kind == "branch":
        if last.value != value:
            return False
    else:
        return False

    return consumed == nibbles
