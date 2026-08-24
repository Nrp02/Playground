from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Optional


@dataclass(frozen=True)
class Message:
    offset: int
    key: Optional[str]
    value: Any
