from __future__ import annotations

import os
from typing import List, Optional

from .memtable import MemTable
from .sstable import SSTable
from .wal import PUT, WriteAheadLog

SSTABLE_PREFIX = "sstable-"
SSTABLE_SUFFIX = ".data"
DEFAULT_MEMTABLE_LIMIT_BYTES = 4096


class LSMStore:
    def __init__(self, directory: str, memtable_limit_bytes: int = DEFAULT_MEMTABLE_LIMIT_BYTES):
        self.directory = directory
        os.makedirs(directory, exist_ok=True)
        self.memtable_limit_bytes = memtable_limit_bytes
        self.wal_path = os.path.join(directory, "wal.log")

        self.memtable = MemTable()
        self.sstables: List[SSTable] = []
        self._next_sequence = 0

        self._load_sstables()
        self._replay_wal()
        self.wal = WriteAheadLog(self.wal_path)

    def _load_sstables(self) -> None:
        loaded = []
        for name in os.listdir(self.directory):
            if name.startswith(SSTABLE_PREFIX) and name.endswith(SSTABLE_SUFFIX):
                sequence = SSTable.sequence_from_filename(name)
                loaded.append(SSTable.load(os.path.join(self.directory, name), sequence))
        loaded.sort(key=lambda s: s.sequence, reverse=True)
        self.sstables = loaded
        if loaded:
            self._next_sequence = loaded[0].sequence + 1

    def _replay_wal(self) -> None:
        for op, key, value in WriteAheadLog.replay(self.wal_path):
            if op == PUT:
                self.memtable.put(key, value)
            else:
                self.memtable.delete(key)

    def put(self, key: str, value: str) -> None:
        self.wal.append_put(key, value)
        self.memtable.put(key, value)
        self._maybe_flush()

    def delete(self, key: str) -> None:
        self.wal.append_delete(key)
        self.memtable.delete(key)
        self._maybe_flush()

    def get(self, key: str, default: Optional[str] = None) -> Optional[str]:
        found, value = self.memtable.get(key)
        if found:
            return default if value is None else value
        for sstable in self.sstables:
            found, value = sstable.get(key)
            if found:
                return default if value is None else value
        return default

    def contains(self, key: str) -> bool:
        _marker = object()
        return self.get(key, _marker) is not _marker

    def _maybe_flush(self) -> None:
        if self.memtable.size_bytes() >= self.memtable_limit_bytes:
            self.flush()

    def flush(self) -> Optional[SSTable]:
        if len(self.memtable) == 0:
            return None
        items = list(self.memtable.sorted_items())
        sequence = self._next_sequence
        self._next_sequence += 1
        path = os.path.join(self.directory, f"{SSTABLE_PREFIX}{sequence:08d}{SSTABLE_SUFFIX}")
        sstable = SSTable.write(path, sequence, items)
        self.sstables.insert(0, sstable)
        self.memtable.clear()
        self.wal.truncate()
        return sstable

    def compact(self) -> Optional[SSTable]:
        self.flush()
        if not self.sstables:
            return None

        merged = {}
        for sstable in reversed(self.sstables):
            for key, value in sstable.items():
                merged[key] = value

        live_items = [(k, v) for k, v in sorted(merged.items()) if v is not None]
        old_paths = [s.path for s in self.sstables]

        sequence = self._next_sequence
        self._next_sequence += 1
        path = os.path.join(self.directory, f"{SSTABLE_PREFIX}{sequence:08d}{SSTABLE_SUFFIX}")
        compacted = SSTable.write(path, sequence, live_items)

        for old_path in old_paths:
            os.remove(old_path)
        self.sstables = [compacted] if live_items else []
        return compacted

    def close(self) -> None:
        self.wal.close()
