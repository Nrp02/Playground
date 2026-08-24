from __future__ import annotations

from typing import List, Tuple

from .errors import RouteNotFoundError


class Router:
    def __init__(self) -> None:
        self._routes: List[Tuple[str, str]] = []

    def add_route(self, prefix: str, backend: str) -> None:
        if not prefix.startswith("/"):
            prefix = "/" + prefix
        self._routes.append((prefix, backend))
        self._routes.sort(key=lambda pair: len(pair[0]), reverse=True)

    def resolve(self, path: str) -> str:
        for prefix, backend in self._routes:
            boundary = prefix if prefix.endswith("/") else prefix + "/"
            if path == prefix or path.startswith(boundary):
                return backend
        raise RouteNotFoundError(path)
