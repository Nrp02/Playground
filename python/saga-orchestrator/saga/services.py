from typing import Any, Dict, Set

from saga.errors import TerminalError, TransientError


class SimulatedParticipant:
    def __init__(
        self,
        name: str,
        fail_transient_times: int = 0,
        fail_terminal: bool = False,
        lose_response_after_apply: bool = False,
        always_fail_compensation: bool = False,
        fail_compensation_times: int = 0,
    ) -> None:
        self.name = name
        self.fail_transient_times = fail_transient_times
        self.fail_terminal = fail_terminal
        self.lose_response_after_apply = lose_response_after_apply
        self.always_fail_compensation = always_fail_compensation
        self.fail_compensation_times = fail_compensation_times
        self.applied: Dict[str, Any] = {}
        self.apply_count = 0
        self.idempotent_hits = 0
        self.compensated: Set[str] = set()
        self.compensation_attempts: Dict[str, int] = {}
        self._transient_attempts: Dict[str, int] = {}
        self._lost_once: Set[str] = set()

    def action(self, key: str, payload: Any) -> Any:
        if key in self.applied:
            self.idempotent_hits += 1
            return self.applied[key]
        if self.fail_terminal:
            raise TerminalError(f"{self.name} rejected {key}")
        attempts = self._transient_attempts.get(key, 0)
        if attempts < self.fail_transient_times:
            self._transient_attempts[key] = attempts + 1
            raise TransientError(f"{self.name} transient failure for {key}")
        result = {"service": self.name, "key": key, "payload": payload}
        self.applied[key] = result
        self.apply_count += 1
        if self.lose_response_after_apply and key not in self._lost_once:
            self._lost_once.add(key)
            raise TransientError(f"{self.name} applied {key} but the response was lost")
        return result

    def compensation(self, key: str, payload: Any) -> None:
        if key in self.compensated:
            return
        if self.always_fail_compensation:
            raise TerminalError(f"{self.name} compensation permanently failing for {key}")
        attempts = self.compensation_attempts.get(key, 0)
        if attempts < self.fail_compensation_times:
            self.compensation_attempts[key] = attempts + 1
            raise TransientError(f"{self.name} compensation transient failure for {key}")
        self.compensated.add(key)
