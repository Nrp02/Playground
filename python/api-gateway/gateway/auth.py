from __future__ import annotations

from typing import Iterable, Set

from .errors import AuthenticationError


class AuthGate:
    def __init__(self, allowed_keys: Iterable[str]) -> None:
        self._allowed_keys: Set[str] = set(allowed_keys)

    def check(self, api_key: str) -> None:
        if not api_key or api_key not in self._allowed_keys:
            raise AuthenticationError(api_key)
