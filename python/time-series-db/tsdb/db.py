from typing import Dict, List, Optional, Tuple

from .series import Series, SeriesKey, make_series_key

DEFAULT_CHUNK_WINDOW_SECONDS = 3600
AGGREGATIONS = ("min", "max", "sum", "count", "avg", "last")


class TimeSeriesDB:
    def __init__(self, chunk_window_seconds: int = DEFAULT_CHUNK_WINDOW_SECONDS,
                 retention_seconds: Optional[int] = None) -> None:
        self.chunk_window_seconds = chunk_window_seconds
        self.retention_seconds = retention_seconds
        self.series: Dict[SeriesKey, Series] = {}

    def ingest(self, metric: str, labels: Dict[str, str], ts: int, value: float) -> None:
        key = make_series_key(metric, labels)
        series = self.series.get(key)
        if series is None:
            series = Series(key, self.chunk_window_seconds)
            self.series[key] = series
        series.add_sample(ts, value)

    def finalize_all(self) -> None:
        for series in self.series.values():
            series.finalize()

    def select(self, metric: Optional[str] = None,
               label_matchers: Optional[Dict[str, str]] = None) -> List[SeriesKey]:
        result = []
        for key in self.series:
            name, labels = key
            if metric is not None and name != metric:
                continue
            if label_matchers:
                labels_dict = dict(labels)
                if any(labels_dict.get(k) != v for k, v in label_matchers.items()):
                    continue
            result.append(key)
        return result

    def range_query(self, metric: Optional[str], start: int, end: int,
                     label_matchers: Optional[Dict[str, str]] = None
                     ) -> Dict[SeriesKey, List[Tuple[int, float]]]:
        result = {}
        for key in self.select(metric, label_matchers):
            result[key] = self.series[key].query_range(start, end)
        return result

    def rollup(self, metric: Optional[str], start: int, end: int, window_size: int,
               aggregations: Optional[List[str]] = None,
               label_matchers: Optional[Dict[str, str]] = None
               ) -> Dict[SeriesKey, Dict[int, Dict[str, float]]]:
        if aggregations is None:
            aggregations = list(AGGREGATIONS)
        for agg in aggregations:
            if agg not in AGGREGATIONS:
                raise ValueError(f"unknown aggregation: {agg}")
        result: Dict[SeriesKey, Dict[int, Dict[str, float]]] = {}
        for key, samples in self.range_query(metric, start, end, label_matchers).items():
            buckets: Dict[int, List[Tuple[int, float]]] = {}
            for ts, value in samples:
                bucket_start = start + ((ts - start) // window_size) * window_size
                buckets.setdefault(bucket_start, []).append((ts, value))
            series_result: Dict[int, Dict[str, float]] = {}
            for bucket_start in sorted(buckets):
                bucket_samples = buckets[bucket_start]
                values = [value for _, value in bucket_samples]
                agg_values: Dict[str, float] = {}
                for agg in aggregations:
                    if agg == "min":
                        agg_values[agg] = min(values)
                    elif agg == "max":
                        agg_values[agg] = max(values)
                    elif agg == "sum":
                        agg_values[agg] = sum(values)
                    elif agg == "count":
                        agg_values[agg] = float(len(values))
                    elif agg == "avg":
                        agg_values[agg] = sum(values) / len(values)
                    elif agg == "last":
                        agg_values[agg] = bucket_samples[-1][1]
                series_result[bucket_start] = agg_values
            result[key] = series_result
        return result

    def apply_retention(self, now: int) -> int:
        if self.retention_seconds is None:
            return 0
        cutoff = now - self.retention_seconds
        evicted = 0
        for series in self.series.values():
            evicted += series.evict_before(cutoff)
        return evicted

    def raw_byte_size(self) -> int:
        return sum(series.raw_byte_size() for series in self.series.values())

    def compressed_byte_size(self) -> int:
        return sum(series.compressed_byte_size() for series in self.series.values())

    def compression_ratio(self) -> float:
        compressed = self.compressed_byte_size()
        if compressed == 0:
            return 0.0
        return self.raw_byte_size() / compressed
