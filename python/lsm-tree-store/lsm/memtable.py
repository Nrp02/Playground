from __future__ import annotations

from typing import Dict, Iterator, Optional, Tuple

TOMBSTONE = None


class MemTable:
    def __init__(self):
        self._data: Dict[str, Optional[str]] = {}
        self._size_bytes = 0

    def put(self, key: str, value: str) -> None:
        self._touch_size(key, value)
        self._data[key] = value

    def delete(self, key: str) -> None:
        self._touch_size(key, "")
        self._data[key] = TOMBSTONE

    def _touch_size(self, key: str, value: str) -> None:
        if key not in self._data:
            self._size_bytes += len(key) + len(value)
        else:
            old = self._data[key] or ""
            self._size_bytes += (len(value) - len(old))

    def get(self, key: str) -> Tuple[bool, Optional[str]]:
        if key in self._data:
            return True, self._data[key]
        return False, None

    def size_bytes(self) -> int:
        return self._size_bytes

    def __len__(self) -> int:
        return len(self._data)

    def sorted_items(self) -> Iterator[Tuple[str, Optional[str]]]:
        for key in sorted(self._data):
            yield key, self._data[key]

    def clear(self) -> None:
        self._data = {}
        self._size_bytes = 0
