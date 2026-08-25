from __future__ import annotations

import threading
import unittest

from reservation import (
    Flight,
    ReservationSystem,
    SeatStatus,
    SeatUnavailableError,
    HoldExpiredError,
    InvalidHoldError,
)


def make_system(hold_duration: float = 30.0) -> ReservationSystem:
    system = ReservationSystem(hold_duration_seconds=hold_duration)
    flight = Flight(flight_number="TEST1", rows=5, seats_per_row=4)
    system.add_flight(flight)
    return system


class TestNormalFlow(unittest.TestCase):
    def test_hold_then_confirm(self) -> None:
        system = make_system()
        token = system.hold_seat("TEST1", "1A", "alice")
        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.status, SeatStatus.HELD)

        system.confirm_booking(token)
        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.status, SeatStatus.BOOKED)
        self.assertEqual(seat.passenger_id, "alice")

    def test_cancel_hold_releases_seat(self) -> None:
        system = make_system()
        token = system.hold_seat("TEST1", "1A", "alice")
        system.cancel_hold(token)
        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.status, SeatStatus.AVAILABLE)
        self.assertIsNone(seat.passenger_id)

    def test_confirm_invalid_token_raises(self) -> None:
        system = make_system()
        with self.assertRaises(InvalidHoldError):
            system.confirm_booking("not-a-real-token")

    def test_double_confirm_raises(self) -> None:
        system = make_system()
        token = system.hold_seat("TEST1", "1A", "alice")
        system.confirm_booking(token)
        with self.assertRaises(InvalidHoldError):
            system.confirm_booking(token)


class TestRejection(unittest.TestCase):
    def test_hold_on_held_seat_rejected(self) -> None:
        system = make_system()
        system.hold_seat("TEST1", "1A", "alice")
        with self.assertRaises(SeatUnavailableError):
            system.hold_seat("TEST1", "1A", "bob")

    def test_hold_on_booked_seat_rejected(self) -> None:
        system = make_system()
        token = system.hold_seat("TEST1", "1A", "alice")
        system.confirm_booking(token)
        with self.assertRaises(SeatUnavailableError):
            system.hold_seat("TEST1", "1A", "bob")


class TestExpiry(unittest.TestCase):
    def test_expired_hold_releases_seat(self) -> None:
        system = make_system(hold_duration=10.0)
        token = system.hold_seat("TEST1", "1A", "alice", now=0.0)
        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.status, SeatStatus.HELD)

        with self.assertRaises(HoldExpiredError):
            system.confirm_booking(token, now=20.0)

        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.status, SeatStatus.AVAILABLE)

    def test_expired_hold_can_be_rebooked(self) -> None:
        system = make_system(hold_duration=10.0)
        system.hold_seat("TEST1", "1A", "alice", now=0.0)
        token2 = system.hold_seat("TEST1", "1A", "bob", now=20.0)
        system.confirm_booking(token2, now=21.0)
        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.status, SeatStatus.BOOKED)
        self.assertEqual(seat.passenger_id, "bob")

    def test_cancel_already_expired_hold_is_noop_ok(self) -> None:
        system = make_system(hold_duration=10.0)
        token = system.hold_seat("TEST1", "1A", "alice", now=0.0)
        system.hold_seat("TEST1", "1A", "bob", now=20.0)
        system.cancel_hold(token)
        seat = system.get_flight("TEST1").get_seat("1A")
        self.assertEqual(seat.passenger_id, "bob")


class TestConcurrency(unittest.TestCase):
    def test_concurrent_stress_one_seat_one_winner(self) -> None:
        for _ in range(5):
            system = make_system()
            successes = []
            failures = []
            lock = threading.Lock()

            def attempt(passenger_id: str) -> None:
                try:
                    token = system.hold_seat("TEST1", "1A", passenger_id)
                    system.confirm_booking(token)
                    with lock:
                        successes.append(passenger_id)
                except Exception:
                    with lock:
                        failures.append(passenger_id)

            threads = [
                threading.Thread(target=attempt, args=(f"passenger-{i}",))
                for i in range(50)
            ]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()

            self.assertEqual(len(successes), 1)
            self.assertEqual(len(failures), 49)
            seat = system.get_flight("TEST1").get_seat("1A")
            self.assertEqual(seat.status, SeatStatus.BOOKED)
            self.assertEqual(seat.passenger_id, successes[0])


if __name__ == "__main__":
    unittest.main()
