from __future__ import annotations


class BufferPoolError(Exception):
    pass


class InvalidPageError(BufferPoolError):
    def __init__(self, page_id: int) -> None:
        super().__init__(f"page {page_id} does not exist on disk")
        self.page_id = page_id


class PoolExhaustedError(BufferPoolError):
    def __init__(self, pool_size: int) -> None:
        super().__init__(f"all {pool_size} frames are pinned, nothing can be evicted")
        self.pool_size = pool_size


class PageStillPinnedError(BufferPoolError):
    def __init__(self, page_id: int, pin_count: int) -> None:
        super().__init__(f"page {page_id} still has {pin_count} pin(s)")
        self.page_id = page_id
        self.pin_count = pin_count


class PageNotResidentError(BufferPoolError):
    def __init__(self, page_id: int) -> None:
        super().__init__(f"page {page_id} is not resident in the buffer pool")
        self.page_id = page_id


class PageFullError(BufferPoolError):
    def __init__(self, needed: int, available: int) -> None:
        super().__init__(f"record needs {needed} bytes, page has {available}")
        self.needed = needed
        self.available = available


class RecordNotFoundError(BufferPoolError):
    def __init__(self, page_id: int, slot: int) -> None:
        super().__init__(f"no record at page {page_id} slot {slot}")
        self.page_id = page_id
        self.slot = slot
