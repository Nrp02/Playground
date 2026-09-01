import json
import os
from typing import Any, Dict, List


class SagaLog:
    def append(self, saga_id: str, event: Dict[str, Any]) -> None:
        raise NotImplementedError

    def events(self, saga_id: str) -> List[Dict[str, Any]]:
        raise NotImplementedError


class InMemorySagaLog(SagaLog):
    def __init__(self) -> None:
        self._events: Dict[str, List[Dict[str, Any]]] = {}

    def append(self, saga_id: str, event: Dict[str, Any]) -> None:
        self._events.setdefault(saga_id, []).append(dict(event))

    def events(self, saga_id: str) -> List[Dict[str, Any]]:
        return list(self._events.get(saga_id, []))


class FileSagaLog(SagaLog):
    def __init__(self, path: str) -> None:
        self.path = path
        if not os.path.exists(path):
            open(path, "a").close()

    def append(self, saga_id: str, event: Dict[str, Any]) -> None:
        record = dict(event)
        record["saga_id"] = saga_id
        with open(self.path, "a") as handle:
            handle.write(json.dumps(record) + "\n")
            handle.flush()
            os.fsync(handle.fileno())

    def events(self, saga_id: str) -> List[Dict[str, Any]]:
        result: List[Dict[str, Any]] = []
        with open(self.path, "r") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                record = json.loads(line)
                if record.get("saga_id") == saga_id:
                    result.append(record)
        return result
