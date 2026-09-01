from typing import List, Tuple

from .encoding import decode_timestamps, decode_values, encode_timestamps, encode_values

RAW_BYTES_PER_SAMPLE = 16


class Chunk:
    def __init__(self, window_start: int, window_end: int) -> None:
        self.window_start = window_start
        self.window_end = window_end
        self.timestamps: List[int] = []
        self.values: List[float] = []
        self.sealed = False
        self.count = 0
        self.encoded_ts: bytes = b""
        self.encoded_val: bytes = b""

    def add(self, ts: int, value: float) -> None:
        if self.sealed:
            raise RuntimeError("cannot add to a sealed chunk")
        self.timestamps.append(ts)
        self.values.append(value)

    def seal(self) -> None:
        if self.sealed:
            return
        self.count = len(self.timestamps)
        self.encoded_ts = encode_timestamps(self.timestamps)
        self.encoded_val = encode_values(self.values)
        self.timestamps = []
        self.values = []
        self.sealed = True

    def decode(self) -> List[Tuple[int, float]]:
        if not self.sealed:
            return list(zip(self.timestamps, self.values))
        ts = decode_timestamps(self.encoded_ts, self.count)
        vals = decode_values(self.encoded_val, self.count)
        return list(zip(ts, vals))

    def sample_count(self) -> int:
        return self.count if self.sealed else len(self.timestamps)

    def raw_byte_size(self) -> int:
        return self.sample_count() * RAW_BYTES_PER_SAMPLE

    def compressed_byte_size(self) -> int:
        if not self.sealed:
            return self.raw_byte_size()
        return len(self.encoded_ts) + len(self.encoded_val)
