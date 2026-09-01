from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class AppMessage:
    src: int
    dest: int
    amount: int


@dataclass(frozen=True)
class Marker:
    snapshot_id: int
    src: int
