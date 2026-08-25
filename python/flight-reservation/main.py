from __future__ import annotations

import threading
import time

from reservation import (
    Flight,
    ReservationSystem,
    HoldError,
)


def race_for_seat(system: ReservationSystem, flight_number: str, seat_id: str) -> None:
    winners = []
    losers = []
    lock = threading.Lock()

    def attempt(passenger_id: str) -> None:
        try:
            token = system.hold_seat(flight_number, seat_id, passenger_id)
            system.confirm_booking(token)
            with lock:
                winners.append(passenger_id)
        except HoldError:
            with lock:
                losers.append(passenger_id)

    threads = [
        threading.Thread(target=attempt, args=(f"passenger-{i}",))
        for i in range(20)
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    print(f"seat {seat_id}: winners={winners} losers={len(losers)}")
    seat = system.get_flight(flight_number).get_seat(seat_id)
    print(f"final seat status: {seat.status}, held/booked by: {seat.passenger_id}")


def expiry_demo(system: ReservationSystem, flight_number: str, seat_id: str) -> None:
    token = system.hold_seat(flight_number, seat_id, "impatient-passenger", now=0.0)
    print(f"held {seat_id} with token {token[:8]}...")
    try:
        system.confirm_booking(token, now=100.0)
    except HoldError as exc:
        print(f"expected expiry failure: {exc}")

    seat = system.get_flight(flight_number).get_seat(seat_id)
    system.hold_seat(flight_number, seat_id, "someone-else", now=100.0)
    seat = system.get_flight(flight_number).get_seat(seat_id)
    print(f"seat {seat_id} rebooked after expiry: status={seat.status}, holder={seat.passenger_id}")


def normal_flow_demo(system: ReservationSystem, flight_number: str, seat_id: str) -> None:
    token = system.hold_seat(flight_number, seat_id, "normal-passenger")
    system.confirm_booking(token)
    seat = system.get_flight(flight_number).get_seat(seat_id)
    print(f"seat {seat_id}: status={seat.status}, passenger={seat.passenger_id}")


def main() -> None:
    system = ReservationSystem(hold_duration_seconds=30.0)
    flight = Flight(flight_number="FR100", rows=10, seats_per_row=6)
    system.add_flight(flight)

    print("=== normal hold -> confirm flow ===")
    normal_flow_demo(system, "FR100", "1A")

    print("\n=== hold expiry then rebook ===")
    expiry_demo(system, "FR100", "2B")

    print("\n=== concurrent race for one seat ===")
    race_for_seat(system, "FR100", "3C")


if __name__ == "__main__":
    main()
