from __future__ import annotations

import os
from typing import Iterator, Optional, Tuple

PUT = "PUT"
DEL = "DEL"


def _escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def _unescape(value: str) -> str:
    out = []
    i = 0
    while i < len(value):
        ch = value[i]
        if ch == "\\" and i + 1 < len(value):
            nxt = value[i + 1]
            if nxt == "n":
                out.append("\n")
            elif nxt == "t":
                out.append("\t")
            elif nxt == "\\":
                out.append("\\")
            else:
                out.append(nxt)
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


class WriteAheadLog:
    def __init__(self, path: str):
        self.path = path
        self._file = open(self.path, "a", encoding="utf-8")

    def append_put(self, key: str, value: str) -> None:
        self._file.write(f"{PUT}\t{_escape(key)}\t{_escape(value)}\n")
        self._file.flush()
        os.fsync(self._file.fileno())

    def append_delete(self, key: str) -> None:
        self._file.write(f"{DEL}\t{_escape(key)}\n")
        self._file.flush()
        os.fsync(self._file.fileno())

    def truncate(self) -> None:
        self._file.close()
        self._file = open(self.path, "w", encoding="utf-8")

    def close(self) -> None:
        self._file.close()

    @staticmethod
    def replay(path: str) -> Iterator[Tuple[str, str, Optional[str]]]:
        if not os.path.exists(path):
            return
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.rstrip("\n")
                if not line:
                    continue
                parts = line.split("\t")
                if parts[0] == PUT and len(parts) == 3:
                    yield PUT, _unescape(parts[1]), _unescape(parts[2])
                elif parts[0] == DEL and len(parts) == 2:
                    yield DEL, _unescape(parts[1]), None
