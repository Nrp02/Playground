from enum import Enum, auto
from typing import Any, Callable, Dict, List, Optional, Set

from saga.errors import SagaCrashed, TerminalError, TransientError
from saga.log import SagaLog
from saga.steps import Step


class SagaStatus(Enum):
    COMPLETED = auto()
    COMPENSATED = auto()
    NEEDS_INTERVENTION = auto()


class _ReplayState:
    def __init__(self) -> None:
        self.completed_order: List[str] = []
        self.compensated: Set[str] = set()
        self.compensating: bool = False
        self.final_status: Optional[SagaStatus] = None


def _replay(events: List[Dict[str, Any]]) -> _ReplayState:
    state = _ReplayState()
    for event in events:
        event_type = event["type"]
        if event_type == "STEP_SUCCEEDED":
            name = event["step"]
            if name not in state.completed_order:
                state.completed_order.append(name)
        elif event_type == "SAGA_COMPENSATING":
            state.compensating = True
        elif event_type == "COMPENSATION_SUCCEEDED":
            state.compensated.add(event["step"])
        elif event_type == "SAGA_COMPLETED":
            state.final_status = SagaStatus.COMPLETED
        elif event_type == "SAGA_COMPENSATED":
            state.final_status = SagaStatus.COMPENSATED
        elif event_type == "SAGA_NEEDS_INTERVENTION":
            state.final_status = SagaStatus.NEEDS_INTERVENTION
    return state


class SagaOrchestrator:
    def __init__(self, log: SagaLog, sleep_fn: Callable[[float], None] = lambda seconds: None) -> None:
        self.log = log
        self.sleep_fn = sleep_fn

    def run(
        self,
        saga_id: str,
        steps: List[Step],
        payload: Any,
        crash_after: Optional[str] = None,
    ) -> SagaStatus:
        events = self.log.events(saga_id)
        state = _replay(events)
        if state.final_status is not None:
            return state.final_status
        if not events:
            self.log.append(saga_id, {"type": "SAGA_STARTED"})

        step_by_name = {step.name: step for step in steps}
        failed_step: Optional[Step] = None

        if not state.compensating:
            start_index = len(state.completed_order)
            for index in range(start_index, len(steps)):
                step = steps[index]
                key = f"{saga_id}:{step.name}"
                succeeded = self._run_step(saga_id, step, key, payload)
                if not succeeded:
                    failed_step = step
                    break
                state.completed_order.append(step.name)
                if crash_after == step.name:
                    raise SagaCrashed(saga_id)

            if failed_step is None:
                self.log.append(saga_id, {"type": "SAGA_COMPLETED"})
                return SagaStatus.COMPLETED

            self.log.append(saga_id, {"type": "SAGA_COMPENSATING", "failed_step": failed_step.name})

        for name in reversed(state.completed_order):
            if name in state.compensated:
                continue
            step = step_by_name[name]
            key = f"{saga_id}:{step.name}"
            succeeded = self._run_compensation(saga_id, step, key, payload)
            if not succeeded:
                self.log.append(saga_id, {"type": "SAGA_NEEDS_INTERVENTION", "step": name})
                return SagaStatus.NEEDS_INTERVENTION
            state.compensated.add(name)

        self.log.append(saga_id, {"type": "SAGA_COMPENSATED"})
        return SagaStatus.COMPENSATED

    def _run_step(self, saga_id: str, step: Step, key: str, payload: Any) -> bool:
        self.log.append(saga_id, {"type": "STEP_STARTED", "step": step.name})
        attempt = 0
        while True:
            try:
                step.action(key, payload)
                self.log.append(saga_id, {"type": "STEP_SUCCEEDED", "step": step.name})
                return True
            except TerminalError:
                self.log.append(saga_id, {"type": "STEP_FAILED", "step": step.name, "terminal": True})
                return False
            except TransientError:
                attempt += 1
                if attempt > step.max_retries:
                    self.log.append(saga_id, {"type": "STEP_FAILED", "step": step.name, "terminal": False})
                    return False
                self.sleep_fn(step.backoff_base * (2 ** (attempt - 1)))

    def _run_compensation(self, saga_id: str, step: Step, key: str, payload: Any) -> bool:
        self.log.append(saga_id, {"type": "COMPENSATION_STARTED", "step": step.name})
        attempt = 0
        while True:
            try:
                step.compensation(key, payload)
                self.log.append(saga_id, {"type": "COMPENSATION_SUCCEEDED", "step": step.name})
                return True
            except Exception:
                attempt += 1
                if attempt > step.compensation_max_retries:
                    self.log.append(saga_id, {"type": "COMPENSATION_FAILED", "step": step.name})
                    return False
                self.sleep_fn(step.backoff_base * (2 ** (attempt - 1)))
