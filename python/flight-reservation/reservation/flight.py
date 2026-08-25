from __future__ import annotations

import string
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Dict, List


class SeatStatus(Enum):
    AVAILABLE = auto()
    HELD = auto()
    BOOKED = auto()


@dataclass
class Seat:
    seat_id: str
    status: SeatStatus = SeatStatus.AVAILABLE
    hold_token: str | None = None
    passenger_id: str | None = None


def _seat_ids(rows: int, seats_per_row: int) -> List[str]:
    letters = string.ascii_uppercase[:seats_per_row]
    ids: List[str] = []
    for row in range(1, rows + 1):
        for letter in letters:
            ids.append(f"{row}{letter}")
    return ids


@dataclass
class Flight:
    flight_number: str
    rows: int
    seats_per_row: int
    seats: Dict[str, Seat] = field(init=False)

    def __post_init__(self) -> None:
        self.seats = {
            seat_id: Seat(seat_id=seat_id)
            for seat_id in _seat_ids(self.rows, self.seats_per_row)
        }

    def seat_ids(self) -> List[str]:
        return list(self.seats.keys())

    def get_seat(self, seat_id: str) -> Seat:
        if seat_id not in self.seats:
            raise KeyError(f"no such seat: {seat_id}")
        return self.seats[seat_id]

    def available_seats(self) -> List[str]:
        return [
            seat_id
            for seat_id, seat in self.seats.items()
            if seat.status == SeatStatus.AVAILABLE
        ]
