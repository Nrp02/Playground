from __future__ import annotations

from typing import Any, List, Optional

from .message import Message


class Partition:
    def __init__(self, partition_id: int):
        self.partition_id = partition_id
        self._log: List[Message] = []

    def append(self, key: Optional[str], value: Any) -> int:
        offset = len(self._log)
        self._log.append(Message(offset=offset, key=key, value=value))
        return offset

    def read_from(self, start_offset: int, max_messages: int = 100) -> List[Message]:
        if start_offset >= len(self._log):
            return []
        end = min(len(self._log), start_offset + max_messages)
        return list(self._log[start_offset:end])

    def __len__(self) -> int:
        return len(self._log)
