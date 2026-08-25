from __future__ import annotations

import threading
import time
import uuid
from dataclasses import dataclass
from typing import Dict, Optional

from .flight import Flight, SeatStatus


class HoldError(Exception):
    pass


class SeatUnavailableError(HoldError):
    pass


class HoldExpiredError(HoldError):
    pass


class InvalidHoldError(HoldError):
    pass


@dataclass
class Hold:
    token: str
    flight_number: str
    seat_id: str
    passenger_id: str
    created_at: float
    expires_at: float
    confirmed: bool = False
    cancelled: bool = False

    def is_expired(self, now: float) -> bool:
        return now >= self.expires_at


class ReservationSystem:
    def __init__(self, hold_duration_seconds: float = 30.0) -> None:
        self._flights: Dict[str, Flight] = {}
        self._holds: Dict[str, Hold] = {}
        self._lock = threading.Lock()
        self.hold_duration_seconds = hold_duration_seconds

    def add_flight(self, flight: Flight) -> None:
        with self._lock:
            self._flights[flight.flight_number] = flight

    def get_flight(self, flight_number: str) -> Flight:
        with self._lock:
            if flight_number not in self._flights:
                raise KeyError(f"no such flight: {flight_number}")
            return self._flights[flight_number]

    def _release_if_expired(self, flight: Flight, seat_id: str, now: float) -> None:
        seat = flight.get_seat(seat_id)
        if seat.status != SeatStatus.HELD or seat.hold_token is None:
            return
        hold = self._holds.get(seat.hold_token)
        if hold is None or hold.is_expired(now):
            seat.status = SeatStatus.AVAILABLE
            seat.hold_token = None
            seat.passenger_id = None
            if hold is not None:
                hold.cancelled = True

    def hold_seat(
        self,
        flight_number: str,
        seat_id: str,
        passenger_id: str,
        now: Optional[float] = None,
    ) -> str:
        now = time.monotonic() if now is None else now
        with self._lock:
            flight = self._flights[flight_number]
            seat = flight.get_seat(seat_id)
            self._release_if_expired(flight, seat_id, now)
            if seat.status != SeatStatus.AVAILABLE:
                raise SeatUnavailableError(
                    f"seat {seat_id} on flight {flight_number} is not available"
                )
            token = uuid.uuid4().hex
            hold = Hold(
                token=token,
                flight_number=flight_number,
                seat_id=seat_id,
                passenger_id=passenger_id,
                created_at=now,
                expires_at=now + self.hold_duration_seconds,
            )
            self._holds[token] = hold
            seat.status = SeatStatus.HELD
            seat.hold_token = token
            seat.passenger_id = passenger_id
            return token

    def confirm_booking(self, hold_token: str, now: Optional[float] = None) -> None:
        now = time.monotonic() if now is None else now
        with self._lock:
            hold = self._holds.get(hold_token)
            if hold is None or hold.cancelled or hold.confirmed:
                raise InvalidHoldError(f"invalid hold token: {hold_token}")
            flight = self._flights[hold.flight_number]
            seat = flight.get_seat(hold.seat_id)
            if hold.is_expired(now):
                self._release_if_expired(flight, hold.seat_id, now)
                raise HoldExpiredError(f"hold {hold_token} has expired")
            if seat.hold_token != hold_token:
                raise InvalidHoldError(f"hold {hold_token} no longer owns its seat")
            seat.status = SeatStatus.BOOKED
            hold.confirmed = True

    def cancel_hold(self, hold_token: str) -> None:
        with self._lock:
            hold = self._holds.get(hold_token)
            if hold is None or hold.confirmed:
                raise InvalidHoldError(f"invalid hold token: {hold_token}")
            if hold.cancelled:
                return
            flight = self._flights[hold.flight_number]
            seat = flight.get_seat(hold.seat_id)
            if seat.hold_token == hold_token:
                seat.status = SeatStatus.AVAILABLE
                seat.hold_token = None
                seat.passenger_id = None
            hold.cancelled = True

    def get_hold(self, hold_token: str) -> Hold:
        with self._lock:
            if hold_token not in self._holds:
                raise InvalidHoldError(f"invalid hold token: {hold_token}")
            return self._holds[hold_token]
