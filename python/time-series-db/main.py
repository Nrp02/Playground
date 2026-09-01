import random

from tsdb import TimeSeriesDB


def ingest_series(db: TimeSeriesDB, metric: str, labels: dict, start_ts: int,
                   interval: int, n: int, base_value: float, rng: random.Random) -> int:
    value = base_value
    ts = start_ts
    for _ in range(n):
        value += rng.uniform(-0.05, 0.05)
        db.ingest(metric, labels, ts, value)
        ts += interval
    return ts


def main() -> int:
    rng = random.Random(42)
    db = TimeSeriesDB(chunk_window_seconds=3600, retention_seconds=None)

    start_ts = 1_700_000_000
    interval = 15
    samples_per_series = 20000

    hosts = ["host-a", "host-b", "host-c"]
    metrics = [("cpu_usage", 40.0), ("mem_usage", 60.0), ("disk_io", 20.0)]

    end_ts = start_ts
    total_samples = 0
    for host in hosts:
        for metric_name, base in metrics:
            labels = {"host": host, "region": "us-east"}
            last_ts = ingest_series(db, metric_name, labels, start_ts, interval,
                                     samples_per_series, base, rng)
            end_ts = max(end_ts, last_ts)
            total_samples += samples_per_series

    db.finalize_all()

    raw = db.raw_byte_size()
    compressed = db.compressed_byte_size()
    ratio = db.compression_ratio()

    print("=== ingest summary ===")
    print(f"total samples ingested: {total_samples}")
    print(f"raw size (16 bytes/sample): {raw} bytes")
    print(f"compressed size: {compressed} bytes")
    print(f"compression ratio: {ratio:.2f}x")

    print()
    print("=== range query ===")
    query_start = start_ts + 1000 * interval
    query_end = query_start + 50 * interval
    results = db.range_query("cpu_usage", query_start, query_end,
                              label_matchers={"host": "host-a"})
    for key, samples in results.items():
        print(f"series {key}: {len(samples)} samples in range")
        for ts, value in samples[:5]:
            print(f"  ts={ts} value={value:.4f}")

    print()
    print("=== downsampled rollup (5 minute windows, avg/min/max) ===")
    rollup_start = start_ts
    rollup_end = start_ts + 3600
    rollups = db.rollup("mem_usage", rollup_start, rollup_end, window_size=300,
                         aggregations=["min", "max", "avg", "count"],
                         label_matchers={"host": "host-b"})
    for key, buckets in rollups.items():
        print(f"series {key}:")
        for bucket_start in sorted(buckets):
            agg = buckets[bucket_start]
            print(f"  window={bucket_start}: min={agg['min']:.3f} max={agg['max']:.3f} "
                  f"avg={agg['avg']:.3f} count={int(agg['count'])}")

    print()
    print("=== retention ===")
    db.retention_seconds = int((end_ts - start_ts) * 0.3)
    before_chunks = sum(len(s.chunks) for s in db.series.values())
    evicted = db.apply_retention(end_ts)
    after_chunks = sum(len(s.chunks) for s in db.series.values())
    print(f"chunks before retention: {before_chunks}")
    print(f"chunks evicted: {evicted}")
    print(f"chunks after retention: {after_chunks}")

    old_query = db.range_query("cpu_usage", start_ts, start_ts + 100,
                                label_matchers={"host": "host-a"})
    for key, samples in old_query.items():
        print(f"post-retention query for {key}: {len(samples)} samples (should be 0 or few)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
