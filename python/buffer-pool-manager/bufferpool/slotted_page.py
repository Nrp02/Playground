from __future__ import annotations

import struct
from typing import Iterator, List, Optional, Tuple

from .errors import PageFullError, RecordNotFoundError

HEADER_SIZE = 4
SLOT_SIZE = 4
TOMBSTONE = 0

_HEADER = struct.Struct(">HH")
_SLOT = struct.Struct(">HH")


class SlottedPage:
    def __init__(self, data: bytearray, initialize: bool = False) -> None:
        self._data = data
        if initialize:
            self.initialize()

    @property
    def data(self) -> bytearray:
        return self._data

    @property
    def capacity(self) -> int:
        return len(self._data)

    def initialize(self) -> None:
        _HEADER.pack_into(self._data, 0, 0, len(self._data))

    @property
    def slot_count(self) -> int:
        return _HEADER.unpack_from(self._data, 0)[0]

    @property
    def free_space_offset(self) -> int:
        return _HEADER.unpack_from(self._data, 0)[1]

    @property
    def free_space(self) -> int:
        num_slots, offset = _HEADER.unpack_from(self._data, 0)
        return offset - (HEADER_SIZE + num_slots * SLOT_SIZE)

    def record_count(self) -> int:
        return sum(1 for _ in self.iter_records())

    def can_fit(self, record: bytes) -> bool:
        return self._reusable_slot(len(record)) is not None or self.free_space >= len(record) + SLOT_SIZE

    def insert(self, record: bytes) -> int:
        num_slots, free_offset = _HEADER.unpack_from(self._data, 0)
        size = len(record)
        if size == 0:
            raise ValueError("cannot store an empty record")
        reuse = self._reusable_slot(size)
        needed = size if reuse is not None else size + SLOT_SIZE
        available = free_offset - (HEADER_SIZE + num_slots * SLOT_SIZE)
        if needed > available:
            raise PageFullError(needed, available)
        new_offset = free_offset - size
        self._data[new_offset:free_offset] = record
        if reuse is not None:
            slot = reuse
        else:
            slot = num_slots
            num_slots += 1
        _SLOT.pack_into(self._data, HEADER_SIZE + slot * SLOT_SIZE, new_offset, size)
        _HEADER.pack_into(self._data, 0, num_slots, new_offset)
        return slot

    def get(self, slot: int) -> bytes:
        offset, size = self._slot_entry(slot)
        if offset == TOMBSTONE:
            raise RecordNotFoundError(-1, slot)
        return bytes(self._data[offset : offset + size])

    def try_get(self, slot: int) -> Optional[bytes]:
        if slot < 0 or slot >= self.slot_count:
            return None
        offset, size = self._slot_entry(slot)
        if offset == TOMBSTONE:
            return None
        return bytes(self._data[offset : offset + size])

    def delete(self, slot: int) -> bool:
        if slot < 0 or slot >= self.slot_count:
            return False
        offset, _ = self._slot_entry(slot)
        if offset == TOMBSTONE:
            return False
        _SLOT.pack_into(self._data, HEADER_SIZE + slot * SLOT_SIZE, TOMBSTONE, 0)
        return True

    def iter_records(self) -> Iterator[Tuple[int, bytes]]:
        for slot in range(self.slot_count):
            offset, size = self._slot_entry(slot)
            if offset == TOMBSTONE:
                continue
            yield slot, bytes(self._data[offset : offset + size])

    def compact(self) -> int:
        live: List[Tuple[int, bytes]] = list(self.iter_records())
        free_space_before = self.free_space
        num_slots = self.slot_count
        write_offset = len(self._data)
        for slot, payload in live:
            write_offset -= len(payload)
            self._data[write_offset : write_offset + len(payload)] = payload
            _SLOT.pack_into(self._data, HEADER_SIZE + slot * SLOT_SIZE, write_offset, len(payload))
        _HEADER.pack_into(self._data, 0, num_slots, write_offset)
        return self.free_space - free_space_before

    def _slot_entry(self, slot: int) -> Tuple[int, int]:
        if slot < 0 or slot >= self.slot_count:
            raise RecordNotFoundError(-1, slot)
        return _SLOT.unpack_from(self._data, HEADER_SIZE + slot * SLOT_SIZE)

    def _reusable_slot(self, size: int) -> Optional[int]:
        for slot in range(self.slot_count):
            offset, _ = self._slot_entry(slot)
            if offset == TOMBSTONE:
                free_offset = self.free_space_offset
                if free_offset - size >= HEADER_SIZE + self.slot_count * SLOT_SIZE:
                    return slot
        return None
