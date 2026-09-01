from typing import Dict, List, Optional, Tuple

from .chunk import Chunk

SeriesKey = Tuple[str, Tuple[Tuple[str, str], ...]]


def make_series_key(metric: str, labels: Dict[str, str]) -> SeriesKey:
    return (metric, tuple(sorted(labels.items())))


class Series:
    def __init__(self, key: SeriesKey, window_size: int) -> None:
        self.key = key
        self.window_size = window_size
        self.chunks: Dict[int, Chunk] = {}
        self._open_window: Optional[int] = None

    def add_sample(self, ts: int, value: float) -> None:
        window_start = (ts // self.window_size) * self.window_size
        if self._open_window is not None and window_start != self._open_window:
            old = self.chunks.get(self._open_window)
            if old is not None and not old.sealed:
                old.seal()
        chunk = self.chunks.get(window_start)
        if chunk is None:
            chunk = Chunk(window_start, window_start + self.window_size)
            self.chunks[window_start] = chunk
        chunk.add(ts, value)
        self._open_window = window_start

    def finalize(self) -> None:
        for chunk in self.chunks.values():
            if not chunk.sealed:
                chunk.seal()

    def evict_before(self, cutoff: int) -> int:
        evicted = 0
        for window_start in list(self.chunks.keys()):
            chunk = self.chunks[window_start]
            if chunk.window_end <= cutoff:
                del self.chunks[window_start]
                evicted += 1
        return evicted

    def query_range(self, start: int, end: int) -> List[Tuple[int, float]]:
        samples: List[Tuple[int, float]] = []
        for window_start, chunk in self.chunks.items():
            if chunk.window_end <= start or window_start >= end:
                continue
            for ts, value in chunk.decode():
                if start <= ts < end:
                    samples.append((ts, value))
        samples.sort(key=lambda pair: pair[0])
        return samples

    def raw_byte_size(self) -> int:
        return sum(chunk.raw_byte_size() for chunk in self.chunks.values())

    def compressed_byte_size(self) -> int:
        return sum(chunk.compressed_byte_size() for chunk in self.chunks.values())
