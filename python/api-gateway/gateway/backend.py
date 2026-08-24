from __future__ import annotations

from .errors import BackendError
from .gateway import Request, Response


def echo_backend(request: Request) -> Response:
    return Response(status=200, body=f"ok:{request.path}")


class FlakyBackend:
    def __init__(self, name: str, healthy: bool = True) -> None:
        self.name = name
        self.healthy = healthy

    def __call__(self, request: Request) -> Response:
        if not self.healthy:
            raise BackendError(f"{self.name} unavailable")
        return Response(status=200, body=f"ok:{request.path}")
