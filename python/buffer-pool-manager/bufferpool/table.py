from __future__ import annotations

from typing import Dict, Iterator, List, NamedTuple, Optional, Tuple

from .buffer_pool import BufferPoolManager
from .errors import PageFullError, RecordNotFoundError
from .slotted_page import HEADER_SIZE, SLOT_SIZE, SlottedPage


class RecordId(NamedTuple):
    page_id: int
    slot: int


class TableHeap:
    def __init__(self, pool: BufferPoolManager) -> None:
        self._pool = pool
        self._page_ids: List[int] = []
        self._free_space: Dict[int, int] = {}
        self._count = 0

    @property
    def page_ids(self) -> List[int]:
        return list(self._page_ids)

    @property
    def page_count(self) -> int:
        return len(self._page_ids)

    def __len__(self) -> int:
        return self._count

    def insert(self, record: bytes) -> RecordId:
        if len(record) + HEADER_SIZE + SLOT_SIZE > self._pool.disk.page_size:
            raise PageFullError(len(record), self._pool.disk.page_size - HEADER_SIZE - SLOT_SIZE)
        target = self._find_page(len(record))
        if target is not None:
            slot: Optional[int] = None
            with self._pool.write_page(target) as page:
                slotted = SlottedPage(page.data)
                try:
                    slot = slotted.insert(record)
                except PageFullError:
                    slot = None
                self._free_space[target] = slotted.free_space
            if slot is not None:
                self._count += 1
                return RecordId(target, slot)
        return self._insert_into_new_page(record)

    def get(self, rid: RecordId) -> bytes:
        if rid.page_id not in self._free_space:
            raise RecordNotFoundError(rid.page_id, rid.slot)
        with self._pool.read_page(rid.page_id) as page:
            payload = SlottedPage(page.data).try_get(rid.slot)
        if payload is None:
            raise RecordNotFoundError(rid.page_id, rid.slot)
        return payload

    def try_get(self, rid: RecordId) -> Optional[bytes]:
        try:
            return self.get(rid)
        except RecordNotFoundError:
            return None

    def delete(self, rid: RecordId) -> bool:
        if rid.page_id not in self._free_space:
            return False
        with self._pool.write_page(rid.page_id) as page:
            slotted = SlottedPage(page.data)
            removed = slotted.delete(rid.slot)
            self._free_space[rid.page_id] = slotted.free_space
        if removed:
            self._count -= 1
        return removed

    def scan(self) -> Iterator[Tuple[RecordId, bytes]]:
        for page_id in self._page_ids:
            with self._pool.read_page(page_id) as page:
                rows = list(SlottedPage(page.data).iter_records())
            for slot, payload in rows:
                yield RecordId(page_id, slot), payload

    def compact_page(self, page_id: int) -> int:
        with self._pool.write_page(page_id) as page:
            slotted = SlottedPage(page.data)
            reclaimed = slotted.compact()
            self._free_space[page_id] = slotted.free_space
        return reclaimed

    def _find_page(self, size: int) -> Optional[int]:
        needed = size + SLOT_SIZE
        for page_id in reversed(self._page_ids):
            if self._free_space.get(page_id, 0) >= needed:
                return page_id
        return None

    def _insert_into_new_page(self, record: bytes) -> RecordId:
        with self._pool.create_page() as page:
            slotted = SlottedPage(page.data, initialize=True)
            slot = slotted.insert(record)
            page_id = page.page_id
            self._free_space[page_id] = slotted.free_space
        self._page_ids.append(page_id)
        self._count += 1
        return RecordId(page_id, slot)
