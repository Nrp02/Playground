from __future__ import annotations

import bisect
import os
from typing import Iterable, List, Optional, Tuple

_TOMBSTONE_MARKER = "\x00TOMBSTONE\x00"


def _escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def _unescape(value: str) -> str:
    out = []
    i = 0
    while i < len(value):
        ch = value[i]
        if ch == "\\" and i + 1 < len(value):
            nxt = value[i + 1]
            out.append({"n": "\n", "t": "\t", "\\": "\\"}.get(nxt, nxt))
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


class SSTable:
    def __init__(self, path: str, sequence: int, keys: List[str], values: List[Optional[str]]):
        self.path = path
        self.sequence = sequence
        self._keys = keys
        self._values = values

    @property
    def min_key(self) -> Optional[str]:
        return self._keys[0] if self._keys else None

    @property
    def max_key(self) -> Optional[str]:
        return self._keys[-1] if self._keys else None

    def __len__(self) -> int:
        return len(self._keys)

    def get(self, key: str) -> Tuple[bool, Optional[str]]:
        if not self._keys or key < self.min_key or key > self.max_key:
            return False, None
        idx = bisect.bisect_left(self._keys, key)
        if idx < len(self._keys) and self._keys[idx] == key:
            return True, self._values[idx]
        return False, None

    def items(self) -> Iterable[Tuple[str, Optional[str]]]:
        return zip(self._keys, self._values)

    @staticmethod
    def write(path: str, sequence: int, sorted_items: List[Tuple[str, Optional[str]]]) -> "SSTable":
        keys: List[str] = []
        values: List[Optional[str]] = []
        with open(path, "w", encoding="utf-8") as fh:
            for key, value in sorted_items:
                serialized = _TOMBSTONE_MARKER if value is None else _escape(value)
                fh.write(f"{_escape(key)}\t{serialized}\n")
                keys.append(key)
                values.append(value)
        return SSTable(path, sequence, keys, values)

    @staticmethod
    def load(path: str, sequence: int) -> "SSTable":
        keys: List[str] = []
        values: List[Optional[str]] = []
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.rstrip("\n")
                if not line:
                    continue
                raw_key, raw_value = line.split("\t", 1)
                key = _unescape(raw_key)
                value = None if raw_value == _TOMBSTONE_MARKER else _unescape(raw_value)
                keys.append(key)
                values.append(value)
        return SSTable(path, sequence, keys, values)

    @staticmethod
    def sequence_from_filename(filename: str) -> int:
        return int(os.path.splitext(filename)[0].split("-")[-1])
