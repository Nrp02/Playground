from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol

from .errors import PermanentChannelError, TransientChannelError


@dataclass
class Notification:
    idempotency_key: str
    subject: str
    body: str


class Channel(Protocol):
    name: str

    def deliver(self, notification: Notification) -> None:
        ...


class SimulatedChannel:
    def __init__(self, name: str, fail_times: int = 0, permanent_failure: bool = False) -> None:
        self.name = name
        self._fail_times = fail_times
        self._permanent_failure = permanent_failure
        self._attempts = 0

    def deliver(self, notification: Notification) -> None:
        self._attempts += 1
        if self._permanent_failure:
            raise PermanentChannelError(f"{self.name} rejected {notification.idempotency_key}")
        if self._attempts <= self._fail_times:
            raise TransientChannelError(
                f"{self.name} transient failure {self._attempts}/{self._fail_times}"
            )

    @property
    def attempts(self) -> int:
        return self._attempts
