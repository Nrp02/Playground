from __future__ import annotations

import threading
from dataclasses import dataclass
from typing import Dict, Iterator, List, Optional

from .disk import INVALID_PAGE_ID, DiskManager
from .errors import (
    PageNotResidentError,
    PageStillPinnedError,
    PoolExhaustedError,
)
from .replacer import Replacer, make_replacer


@dataclass
class BufferPoolStats:
    hits: int = 0
    misses: int = 0
    evictions: int = 0
    dirty_writebacks: int = 0
    clean_evictions: int = 0

    @property
    def lookups(self) -> int:
        return self.hits + self.misses

    @property
    def hit_rate(self) -> float:
        total = self.lookups
        return self.hits / total if total else 0.0

    def reset(self) -> None:
        self.hits = 0
        self.misses = 0
        self.evictions = 0
        self.dirty_writebacks = 0
        self.clean_evictions = 0


class Frame:
    __slots__ = ("frame_id", "page_id", "data", "pin_count", "is_dirty")

    def __init__(self, frame_id: int, page_size: int) -> None:
        self.frame_id = frame_id
        self.page_id = INVALID_PAGE_ID
        self.data = bytearray(page_size)
        self.pin_count = 0
        self.is_dirty = False

    def reset(self, page_id: int, data: bytearray) -> None:
        self.page_id = page_id
        self.data = data
        self.pin_count = 0
        self.is_dirty = False


class Page:
    def __init__(self, pool: "BufferPoolManager", frame: Frame) -> None:
        self._pool = pool
        self._frame = frame
        self._page_id = frame.page_id

    @property
    def page_id(self) -> int:
        return self._page_id

    @property
    def data(self) -> bytearray:
        return self._frame.data

    @property
    def pin_count(self) -> int:
        return self._frame.pin_count

    @property
    def is_dirty(self) -> bool:
        return self._frame.is_dirty

    def read(self, offset: int = 0, length: Optional[int] = None) -> bytes:
        end = len(self._frame.data) if length is None else offset + length
        return bytes(self._frame.data[offset:end])

    def write(self, payload: bytes, offset: int = 0) -> None:
        end = offset + len(payload)
        if offset < 0 or end > len(self._frame.data):
            raise ValueError("write would run past the end of the page")
        self._frame.data[offset:end] = payload
        self._frame.is_dirty = True

    def mark_dirty(self) -> None:
        self._frame.is_dirty = True

    def unpin(self, is_dirty: bool = False) -> bool:
        return self._pool.unpin_page(self._page_id, is_dirty)


class PageGuard:
    def __init__(self, pool: "BufferPoolManager", page: Page, writable: bool) -> None:
        self._pool = pool
        self._page = page
        self._writable = writable
        self._released = False

    def __enter__(self) -> Page:
        return self._page

    def __exit__(self, exc_type: Optional[type], exc: Optional[BaseException], tb: object) -> None:
        self.release()

    def release(self) -> None:
        if self._released:
            return
        self._released = True
        self._pool.unpin_page(self._page.page_id, self._writable or self._page.is_dirty)


class BufferPoolManager:
    def __init__(
        self,
        disk: DiskManager,
        pool_size: int,
        policy: str = "lru-k",
        k: int = 2,
        replacer: Optional[Replacer] = None,
    ) -> None:
        if pool_size <= 0:
            raise ValueError("pool_size must be positive")
        self._disk = disk
        self._pool_size = pool_size
        self._frames: List[Frame] = [Frame(i, disk.page_size) for i in range(pool_size)]
        self._page_table: Dict[int, int] = {}
        self._free_list: List[int] = list(range(pool_size))
        self._replacer: Replacer = replacer if replacer is not None else make_replacer(policy, k)
        self._lock = threading.RLock()
        self.stats = BufferPoolStats()

    @property
    def pool_size(self) -> int:
        return self._pool_size

    @property
    def policy(self) -> str:
        return self._replacer.name

    @property
    def disk(self) -> DiskManager:
        return self._disk

    def resident_page_ids(self) -> List[int]:
        with self._lock:
            return sorted(self._page_table)

    def free_frames(self) -> int:
        with self._lock:
            return len(self._free_list)

    def pin_count(self, page_id: int) -> int:
        with self._lock:
            frame_id = self._page_table.get(page_id)
            if frame_id is None:
                raise PageNotResidentError(page_id)
            return self._frames[frame_id].pin_count

    def is_dirty(self, page_id: int) -> bool:
        with self._lock:
            frame_id = self._page_table.get(page_id)
            if frame_id is None:
                raise PageNotResidentError(page_id)
            return self._frames[frame_id].is_dirty

    def new_page(self) -> Page:
        with self._lock:
            frame = self._grab_frame()
            page_id = self._disk.allocate_page()
            frame.reset(page_id, bytearray(self._disk.page_size))
            frame.pin_count = 1
            frame.is_dirty = True
            self._page_table[page_id] = frame.frame_id
            self._replacer.record_access(frame.frame_id)
            self._replacer.set_evictable(frame.frame_id, False)
            return Page(self, frame)

    def fetch_page(self, page_id: int) -> Page:
        with self._lock:
            frame_id = self._page_table.get(page_id)
            if frame_id is not None:
                frame = self._frames[frame_id]
                frame.pin_count += 1
                self._replacer.record_access(frame_id)
                self._replacer.set_evictable(frame_id, False)
                self.stats.hits += 1
                return Page(self, frame)
            self.stats.misses += 1
            frame = self._grab_frame()
            data = self._disk.read_page(page_id)
            frame.reset(page_id, data)
            frame.pin_count = 1
            self._page_table[page_id] = frame.frame_id
            self._replacer.record_access(frame.frame_id)
            self._replacer.set_evictable(frame.frame_id, False)
            return Page(self, frame)

    def unpin_page(self, page_id: int, is_dirty: bool = False) -> bool:
        with self._lock:
            frame_id = self._page_table.get(page_id)
            if frame_id is None:
                return False
            frame = self._frames[frame_id]
            if frame.pin_count <= 0:
                return False
            frame.pin_count -= 1
            if is_dirty:
                frame.is_dirty = True
            if frame.pin_count == 0:
                self._replacer.set_evictable(frame_id, True)
            return True

    def flush_page(self, page_id: int) -> bool:
        with self._lock:
            frame_id = self._page_table.get(page_id)
            if frame_id is None:
                return False
            frame = self._frames[frame_id]
            self._disk.write_page(page_id, bytes(frame.data))
            frame.is_dirty = False
            return True

    def flush_all_pages(self) -> int:
        with self._lock:
            flushed = 0
            for page_id in list(self._page_table):
                if self.flush_page(page_id):
                    flushed += 1
            return flushed

    def delete_page(self, page_id: int) -> bool:
        with self._lock:
            frame_id = self._page_table.get(page_id)
            if frame_id is None:
                self._disk.deallocate_page(page_id)
                return True
            frame = self._frames[frame_id]
            if frame.pin_count > 0:
                raise PageStillPinnedError(page_id, frame.pin_count)
            self._replacer.remove(frame_id)
            del self._page_table[page_id]
            frame.page_id = INVALID_PAGE_ID
            frame.is_dirty = False
            frame.data = bytearray(self._disk.page_size)
            self._free_list.append(frame_id)
            self._disk.deallocate_page(page_id)
            return True

    def read_page(self, page_id: int) -> PageGuard:
        return PageGuard(self, self.fetch_page(page_id), writable=False)

    def write_page(self, page_id: int) -> PageGuard:
        return PageGuard(self, self.fetch_page(page_id), writable=True)

    def create_page(self) -> PageGuard:
        return PageGuard(self, self.new_page(), writable=True)

    def pinned_page_ids(self) -> List[int]:
        with self._lock:
            return sorted(
                page_id
                for page_id, frame_id in self._page_table.items()
                if self._frames[frame_id].pin_count > 0
            )

    def frame_snapshot(self) -> Iterator[Frame]:
        with self._lock:
            return iter(list(self._frames))

    def _grab_frame(self) -> Frame:
        if self._free_list:
            return self._frames[self._free_list.pop()]
        victim_id = self._replacer.evict()
        if victim_id is None:
            raise PoolExhaustedError(self._pool_size)
        frame = self._frames[victim_id]
        if frame.is_dirty:
            self._disk.write_page(frame.page_id, bytes(frame.data))
            self.stats.dirty_writebacks += 1
        else:
            self.stats.clean_evictions += 1
        self.stats.evictions += 1
        del self._page_table[frame.page_id]
        frame.page_id = INVALID_PAGE_ID
        frame.is_dirty = False
        return frame
