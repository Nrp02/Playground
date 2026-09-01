from dataclasses import dataclass
from typing import Any, Callable

ActionFn = Callable[[str, Any], Any]
CompensationFn = Callable[[str, Any], Any]


@dataclass
class Step:
    name: str
    action: ActionFn
    compensation: CompensationFn
    max_retries: int = 3
    compensation_max_retries: int = 3
    backoff_base: float = 0.01
