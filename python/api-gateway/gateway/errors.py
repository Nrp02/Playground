from __future__ import annotations


class GatewayError(Exception):
    pass


class AuthenticationError(GatewayError):
    pass


class RouteNotFoundError(GatewayError):
    pass


class RateLimitExceededError(GatewayError):
    pass


class CircuitOpenError(GatewayError):
    pass


class BackendError(GatewayError):
    pass
