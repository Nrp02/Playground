import random
import struct
import unittest

from tsdb.bitio import BitReader, BitWriter
from tsdb.chunk import Chunk
from tsdb.db import TimeSeriesDB
from tsdb.encoding import decode_timestamps, decode_values, encode_timestamps, encode_values
from tsdb.series import make_series_key


def bits_of(value: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", value))[0]


class TestBitIO(unittest.TestCase):
    def test_round_trip_mixed_widths(self) -> None:
        bw = BitWriter()
        bw.write_bit(1)
        bw.write_bit(0)
        bw.write_bits(0b10110, 5)
        bw.write_int(-13, 9)
        bw.write_bits(0xABCD, 16)
        data = bw.getvalue()

        br = BitReader(data)
        self.assertEqual(br.read_bit(), 1)
        self.assertEqual(br.read_bit(), 0)
        self.assertEqual(br.read_bits(5), 0b10110)
        self.assertEqual(br.read_int(9), -13)
        self.assertEqual(br.read_bits(16), 0xABCD)

    def test_signed_boundaries(self) -> None:
        for nbits in (7, 9, 12):
            low = -(1 << (nbits - 1))
            high = (1 << (nbits - 1)) - 1
            bw = BitWriter()
            bw.write_int(low, nbits)
            bw.write_int(high, nbits)
            bw.write_int(0, nbits)
            br = BitReader(bw.getvalue())
            self.assertEqual(br.read_int(nbits), low)
            self.assertEqual(br.read_int(nbits), high)
            self.assertEqual(br.read_int(nbits), 0)


class TestTimestampEncoding(unittest.TestCase):
    def test_round_trip_regular(self) -> None:
        timestamps = [1000 + i * 15 for i in range(500)]
        encoded = encode_timestamps(timestamps)
        decoded = decode_timestamps(encoded, len(timestamps))
        self.assertEqual(decoded, timestamps)

    def test_round_trip_irregular(self) -> None:
        rng = random.Random(1)
        timestamps = [1000]
        for _ in range(300):
            timestamps.append(timestamps[-1] + rng.choice([1, 2, 5, 30, 100, 9999, -5]))
        encoded = encode_timestamps(timestamps)
        decoded = decode_timestamps(encoded, len(timestamps))
        self.assertEqual(decoded, timestamps)

    def test_empty_and_single(self) -> None:
        self.assertEqual(decode_timestamps(encode_timestamps([]), 0), [])
        self.assertEqual(decode_timestamps(encode_timestamps([42]), 1), [42])

    def test_regular_data_compresses_well(self) -> None:
        timestamps = [1000 + i * 15 for i in range(1000)]
        encoded = encode_timestamps(timestamps)
        self.assertLess(len(encoded), len(timestamps) * 8 // 4)


class TestValueEncoding(unittest.TestCase):
    def test_round_trip_randomized_floats(self) -> None:
        rng = random.Random(2)
        values = [rng.uniform(-1000, 1000) for _ in range(500)]
        encoded = encode_values(values)
        decoded = decode_values(encoded, len(values))
        self.assertEqual(len(decoded), len(values))
        for original, result in zip(values, decoded):
            self.assertEqual(bits_of(original), bits_of(result))

    def test_round_trip_special_values(self) -> None:
        values = [0.0, -0.0, 1.0, -1.0, float("inf"), float("-inf"), float("nan"),
                  123456.789, -0.0001, 42.0]
        encoded = encode_values(values)
        decoded = decode_values(encoded, len(values))
        self.assertEqual(len(decoded), len(values))
        for original, result in zip(values, decoded):
            self.assertEqual(bits_of(original), bits_of(result))

    def test_constant_series(self) -> None:
        values = [3.14159] * 200
        encoded = encode_values(values)
        decoded = decode_values(encoded, len(values))
        self.assertEqual(decoded, values)
        self.assertLess(len(encoded), len(values) * 8 // 4)

    def test_empty_and_single(self) -> None:
        self.assertEqual(decode_values(encode_values([]), 0), [])
        decoded = decode_values(encode_values([5.5]), 1)
        self.assertEqual(decoded, [5.5])

    def test_slowly_changing_compresses_well(self) -> None:
        rng = random.Random(3)
        values = []
        value = 50.0
        for _ in range(2000):
            value += rng.uniform(-0.01, 0.01)
            values.append(value)
        encoded = encode_values(values)
        self.assertLess(len(encoded), len(values) * 8)


class TestChunk(unittest.TestCase):
    def test_decode_before_and_after_seal(self) -> None:
        chunk = Chunk(0, 3600)
        samples = [(i * 15, 10.0 + i * 0.01) for i in range(100)]
        for ts, value in samples:
            chunk.add(ts, value)
        self.assertEqual(chunk.decode(), samples)
        chunk.seal()
        decoded = chunk.decode()
        self.assertEqual(len(decoded), len(samples))
        for (ts_a, val_a), (ts_b, val_b) in zip(samples, decoded):
            self.assertEqual(ts_a, ts_b)
            self.assertEqual(bits_of(val_a), bits_of(val_b))

    def test_compressed_smaller_than_raw(self) -> None:
        chunk = Chunk(0, 3600)
        for i in range(500):
            chunk.add(i * 15, 100.0 + i * 0.001)
        chunk.seal()
        self.assertLess(chunk.compressed_byte_size(), chunk.raw_byte_size())


class TestTimeSeriesDB(unittest.TestCase):
    def _build_db(self) -> TimeSeriesDB:
        db = TimeSeriesDB(chunk_window_seconds=600)
        rng = random.Random(4)
        value = 50.0
        for i in range(300):
            value += rng.uniform(-0.02, 0.02)
            db.ingest("cpu", {"host": "a", "dc": "us"}, i * 10, value)
        value = 80.0
        for i in range(300):
            value += rng.uniform(-0.02, 0.02)
            db.ingest("cpu", {"host": "b", "dc": "us"}, i * 10, value)
        db.finalize_all()
        return db

    def test_label_selection(self) -> None:
        db = self._build_db()
        keys = db.select("cpu", {"host": "a"})
        self.assertEqual(len(keys), 1)
        self.assertEqual(dict(keys[0][1])["host"], "a")

        keys_missing = db.select("cpu", {"host": "nonexistent"})
        self.assertEqual(keys_missing, [])

    def test_range_query_boundaries(self) -> None:
        db = TimeSeriesDB(chunk_window_seconds=1000)
        for ts in range(0, 100, 10):
            db.ingest("m", {"h": "x"}, ts, float(ts))
        db.finalize_all()

        results = db.range_query("m", 10, 50, label_matchers={"h": "x"})
        samples = list(results.values())[0]
        timestamps = [ts for ts, _ in samples]
        self.assertIn(10, timestamps)
        self.assertNotIn(50, timestamps)
        self.assertEqual(timestamps, sorted(timestamps))
        self.assertTrue(all(10 <= ts < 50 for ts in timestamps))

    def test_rollup_matches_brute_force(self) -> None:
        db = TimeSeriesDB(chunk_window_seconds=1000)
        rng = random.Random(5)
        raw_samples = []
        for i in range(200):
            ts = i * 3
            value = rng.uniform(0, 100)
            db.ingest("m", {"h": "y"}, ts, value)
            raw_samples.append((ts, value))
        db.finalize_all()

        window_size = 50
        rollups = db.rollup("m", 0, 600, window_size,
                             aggregations=["min", "max", "sum", "count", "avg", "last"],
                             label_matchers={"h": "y"})
        key = make_series_key("m", {"h": "y"})
        buckets = rollups[key]

        brute_buckets = {}
        for ts, value in raw_samples:
            if not (0 <= ts < 600):
                continue
            bucket_start = (ts // window_size) * window_size
            brute_buckets.setdefault(bucket_start, []).append(value)

        for bucket_start, values in brute_buckets.items():
            agg = buckets[bucket_start]
            self.assertAlmostEqual(agg["min"], min(values))
            self.assertAlmostEqual(agg["max"], max(values))
            self.assertAlmostEqual(agg["sum"], sum(values))
            self.assertEqual(agg["count"], len(values))
            self.assertAlmostEqual(agg["avg"], sum(values) / len(values))
            self.assertAlmostEqual(agg["last"], values[-1])

    def test_retention_eviction(self) -> None:
        db = TimeSeriesDB(chunk_window_seconds=100, retention_seconds=250)
        for i in range(20):
            db.ingest("m", {"h": "z"}, i * 30, float(i))
        db.finalize_all()

        now = 20 * 30
        before = sum(len(s.chunks) for s in db.series.values())
        evicted = db.apply_retention(now)
        after = sum(len(s.chunks) for s in db.series.values())

        self.assertGreater(evicted, 0)
        self.assertEqual(before - evicted, after)

        results = db.range_query("m", 0, 100, label_matchers={"h": "z"})
        samples = list(results.values())[0]
        self.assertEqual(samples, [])

    def test_compression_ratio_beats_raw(self) -> None:
        db = TimeSeriesDB(chunk_window_seconds=3600)
        rng = random.Random(6)
        value = 20.0
        for i in range(2000):
            value += rng.uniform(-0.01, 0.01)
            db.ingest("m", {"h": "w"}, i * 15, value)
        db.finalize_all()
        self.assertGreater(db.compression_ratio(), 1.0)


if __name__ == "__main__":
    unittest.main()
