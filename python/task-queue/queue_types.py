from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Optional


class JobStatus(Enum):
    PENDING = "pending"
    RUNNING = "running"
    DONE = "done"
    FAILED = "failed"


@dataclass
class Job:
    job_id: int
    func_name: str
    args: tuple = ()
    kwargs: dict = field(default_factory=dict)
    max_retries: int = 3
    attempt: int = 0


@dataclass
class JobResult:
    job_id: int
    status: JobStatus
    result: Any = None
    error: Optional[str] = None
    attempts: int = 0


REGISTRY: dict[str, Callable] = {}


def register(name: str):
    def deco(fn):
        REGISTRY[name] = fn
        return fn
    return deco
