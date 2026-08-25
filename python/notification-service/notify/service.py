from __future__ import annotations

import random
import time
from dataclasses import dataclass, field
from typing import Callable, Dict, List

from .backoff import exponential_backoff_with_jitter
from .channels import Channel, Notification
from .errors import PermanentChannelError, TransientChannelError


@dataclass
class ChannelOutcome:
    channel_name: str
    succeeded: bool
    attempts: int
    error: str = ""


@dataclass
class DeliveryResult:
    idempotency_key: str
    outcomes: Dict[str, ChannelOutcome] = field(default_factory=dict)
    deduplicated: bool = False

    def succeeded_channels(self) -> List[str]:
        return [name for name, outcome in self.outcomes.items() if outcome.succeeded]

    def failed_channels(self) -> List[str]:
        return [name for name, outcome in self.outcomes.items() if not outcome.succeeded]


class NotificationService:
    def __init__(
        self,
        max_retries: int = 3,
        base_delay: float = 0.1,
        max_delay: float = 2.0,
        sleep_fn: Callable[[float], None] = time.sleep,
        rng: Callable[[], float] = random.random,
    ) -> None:
        self._max_retries = max_retries
        self._base_delay = base_delay
        self._max_delay = max_delay
        self._sleep_fn = sleep_fn
        self._rng = rng
        self._history: Dict[str, DeliveryResult] = {}

    def send(self, notification: Notification, channels: List[Channel]) -> DeliveryResult:
        existing = self._history.get(notification.idempotency_key)
        if existing is not None:
            return DeliveryResult(
                idempotency_key=notification.idempotency_key,
                outcomes=existing.outcomes,
                deduplicated=True,
            )
        result = DeliveryResult(idempotency_key=notification.idempotency_key)
        for channel in channels:
            result.outcomes[channel.name] = self._deliver_with_retry(notification, channel)
        self._history[notification.idempotency_key] = result
        return result

    def _deliver_with_retry(self, notification: Notification, channel: Channel) -> ChannelOutcome:
        attempt = 0
        last_error = ""
        while attempt < self._max_retries:
            attempt += 1
            try:
                channel.deliver(notification)
                return ChannelOutcome(channel_name=channel.name, succeeded=True, attempts=attempt)
            except PermanentChannelError as exc:
                return ChannelOutcome(
                    channel_name=channel.name,
                    succeeded=False,
                    attempts=attempt,
                    error=str(exc),
                )
            except TransientChannelError as exc:
                last_error = str(exc)
                if attempt < self._max_retries:
                    delay = exponential_backoff_with_jitter(
                        attempt, self._base_delay, self._max_delay, self._rng
                    )
                    self._sleep_fn(delay)
        return ChannelOutcome(
            channel_name=channel.name,
            succeeded=False,
            attempts=attempt,
            error=last_error,
        )

    def get_result(self, idempotency_key: str) -> DeliveryResult:
        return self._history[idempotency_key]

    def history(self) -> Dict[str, DeliveryResult]:
        return dict(self._history)
