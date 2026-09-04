from __future__ import annotations

import os
import threading
from pathlib import Path
from typing import IO, List, Optional, Union

from .errors import InvalidPageError

PAGE_SIZE = 4096
INVALID_PAGE_ID = -1


class DiskManager:
    def __init__(self, path: Union[str, os.PathLike], page_size: int = PAGE_SIZE) -> None:
        if page_size <= 0:
            raise ValueError("page_size must be positive")
        self._path = Path(path)
        self._page_size = page_size
        self._lock = threading.Lock()
        existed = self._path.exists()
        length = self._path.stat().st_size if existed else 0
        if length % page_size != 0:
            raise ValueError(f"file {self._path} is not a whole number of {page_size}-byte pages")
        self._file: IO[bytes] = open(self._path, "r+b" if existed else "w+b")
        self._num_pages = length // page_size
        self._free_page_ids: List[int] = []
        self._reads = 0
        self._writes = 0
        self._allocations = 0
        self._closed = False

    @property
    def page_size(self) -> int:
        return self._page_size

    @property
    def path(self) -> Path:
        return self._path

    @property
    def num_pages(self) -> int:
        with self._lock:
            return self._num_pages

    @property
    def reads(self) -> int:
        with self._lock:
            return self._reads

    @property
    def writes(self) -> int:
        with self._lock:
            return self._writes

    @property
    def allocations(self) -> int:
        with self._lock:
            return self._allocations

    def reset_counters(self) -> None:
        with self._lock:
            self._reads = 0
            self._writes = 0

    def allocate_page(self) -> int:
        with self._lock:
            self._check_open()
            self._allocations += 1
            if self._free_page_ids:
                page_id = self._free_page_ids.pop()
                self._write_locked(page_id, bytes(self._page_size))
                return page_id
            page_id = self._num_pages
            self._num_pages += 1
            self._write_locked(page_id, bytes(self._page_size))
            return page_id

    def deallocate_page(self, page_id: int) -> None:
        with self._lock:
            self._check_open()
            self._validate_locked(page_id)
            if page_id not in self._free_page_ids:
                self._free_page_ids.append(page_id)

    def read_page(self, page_id: int) -> bytearray:
        with self._lock:
            self._check_open()
            self._validate_locked(page_id)
            self._file.seek(page_id * self._page_size)
            raw = self._file.read(self._page_size)
            self._reads += 1
            if len(raw) < self._page_size:
                raw = raw + bytes(self._page_size - len(raw))
            return bytearray(raw)

    def write_page(self, page_id: int, data: bytes) -> None:
        with self._lock:
            self._check_open()
            self._validate_locked(page_id)
            self._write_locked(page_id, data)

    def flush(self) -> None:
        with self._lock:
            if not self._closed:
                self._file.flush()
                os.fsync(self._file.fileno())

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._file.flush()
            self._file.close()
            self._closed = True

    def remove(self) -> None:
        self.close()
        if self._path.exists():
            self._path.unlink()

    def _write_locked(self, page_id: int, data: bytes) -> None:
        if len(data) > self._page_size:
            raise ValueError(f"page payload of {len(data)} bytes exceeds page size {self._page_size}")
        payload = bytes(data)
        if len(payload) < self._page_size:
            payload = payload + bytes(self._page_size - len(payload))
        self._file.seek(page_id * self._page_size)
        self._file.write(payload)
        self._file.flush()
        self._writes += 1

    def _validate_locked(self, page_id: int) -> None:
        if page_id < 0 or page_id >= self._num_pages:
            raise InvalidPageError(page_id)

    def _check_open(self) -> None:
        if self._closed:
            raise ValueError("disk manager is closed")

    def __enter__(self) -> "DiskManager":
        return self

    def __exit__(self, exc_type: Optional[type], exc: Optional[BaseException], tb: object) -> None:
        self.close()
