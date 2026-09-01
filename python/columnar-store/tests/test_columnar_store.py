import random
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict, List

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from columnar import encodings
from columnar.predicates import Predicate
from columnar.reader import ColumnarReader
from columnar.schema import Schema
from columnar.writer import write


def write_and_read(schema: Schema, rows: List[Dict[str, Any]], row_group_size: int = 4096) -> ColumnarReader:
    tmp = tempfile.NamedTemporaryFile(suffix=".columnar", delete=False)
    tmp.close()
    write(tmp.name, schema, rows, row_group_size=row_group_size)
    return ColumnarReader(tmp.name)


class TestEncodingsRoundTrip(unittest.TestCase):
    def test_plain_int(self):
        values = [1, -5, 1000000, 0, 42]
        data = encodings.encode_plain("int", values)
        self.assertEqual(encodings.decode_plain("int", data, len(values)), values)

    def test_plain_float(self):
        values = [1.5, -2.25, 0.0, 3.14159]
        data = encodings.encode_plain("float", values)
        self.assertEqual(encodings.decode_plain("float", data, len(values)), values)

    def test_plain_str(self):
        values = ["hello", "", "world!", "unicode-éè"]
        data = encodings.encode_plain("str", values)
        self.assertEqual(encodings.decode_plain("str", data, len(values)), values)

    def test_plain_bool(self):
        values = [True, False, False, True]
        data = encodings.encode_plain("bool", values)
        self.assertEqual(encodings.decode_plain("bool", data, len(values)), values)

    def test_rle_round_trip(self):
        values = ["a", "a", "a", "b", "b", "c", "c", "c", "c"]
        data = encodings.encode_rle("str", values)
        self.assertEqual(encodings.decode_rle("str", data, len(values)), values)

    def test_rle_int(self):
        values = [5, 5, 5, 5, 6, 6, 7]
        data = encodings.encode_rle("int", values)
        self.assertEqual(encodings.decode_rle("int", data, len(values)), values)

    def test_dictionary_round_trip(self):
        values = ["cat", "dog", "cat", "cat", "bird", "dog"]
        data = encodings.encode_dictionary("str", values)
        self.assertEqual(encodings.decode_dictionary("str", data, len(values)), values)

    def test_bitpack_round_trip(self):
        values = [10, 20, 15, 10, 30, 5, 25]
        data = encodings.encode_bitpack("int", values)
        self.assertEqual(encodings.decode_bitpack("int", data, len(values)), values)

    def test_bitpack_negative_range(self):
        values = [-10, -5, 0, 5, 10, -10, 3]
        data = encodings.encode_bitpack("int", values)
        self.assertEqual(encodings.decode_bitpack("int", data, len(values)), values)

    def test_bitmap_round_trip(self):
        mask = [True, False, False, True, True, False, True, False, True]
        packed = encodings.pack_bitmap(mask)
        self.assertEqual(encodings.unpack_bitmap(packed, len(mask)), mask)


class TestEncodingSelection(unittest.TestCase):
    def test_selects_rle_for_long_runs(self):
        values = ["x"] * 50 + ["y"] * 50
        self.assertEqual(encodings.choose_encoding("str", values), encodings.RLE)

    def test_selects_dictionary_for_low_cardinality(self):
        rng = random.Random(1)
        values = [rng.choice(["a", "b", "c"]) for _ in range(300)]
        self.assertEqual(encodings.choose_encoding("str", values), encodings.DICTIONARY)

    def test_selects_plain_for_high_cardinality_strings(self):
        values = [f"unique-{i}" for i in range(300)]
        self.assertEqual(encodings.choose_encoding("str", values), encodings.PLAIN)

    def test_selects_bitpack_for_small_range_ints(self):
        rng = random.Random(2)
        values = [rng.randint(0, 15) for _ in range(300)]
        self.assertEqual(encodings.choose_encoding("int", values), encodings.BITPACK)

    def test_selects_plain_for_wide_range_ints(self):
        rng = random.Random(3)
        values = [rng.randint(0, 2 ** 40) for _ in range(300)]
        self.assertEqual(encodings.choose_encoding("int", values), encodings.PLAIN)


class TestWriterReader(unittest.TestCase):
    def make_schema(self) -> Schema:
        return Schema({
            "id": "int",
            "amount": "float",
            "label": "str",
            "active": "bool",
        })

    def make_rows(self, n: int) -> List[Dict[str, Any]]:
        rng = random.Random(7)
        rows = []
        for i in range(n):
            rows.append({
                "id": i,
                "amount": rng.uniform(0, 100),
                "label": rng.choice(["red", "green", "blue"]),
                "active": rng.random() < 0.5,
            })
        return rows

    def test_full_scan_round_trip(self):
        schema = self.make_schema()
        rows = self.make_rows(500)
        reader = write_and_read(schema, rows, row_group_size=64)
        result, stats = reader.full_scan()
        self.assertEqual(result, rows)
        self.assertEqual(stats.row_groups_skipped, 0)

    def test_projection_returns_only_requested_columns(self):
        schema = self.make_schema()
        rows = self.make_rows(200)
        reader = write_and_read(schema, rows, row_group_size=32)
        result, _ = reader.scan(columns=["id", "label"])
        for row in result:
            self.assertEqual(set(row.keys()), {"id", "label"})
        self.assertEqual([r["id"] for r in result], [r["id"] for r in rows])

    def test_filtered_scan_matches_brute_force(self):
        schema = self.make_schema()
        rng = random.Random(11)
        rows = []
        for i in range(3000):
            rows.append({
                "id": i,
                "amount": float(i % 500),
                "label": rng.choice(["red", "green", "blue"]),
                "active": rng.random() < 0.5,
            })
        reader = write_and_read(schema, rows, row_group_size=128)

        predicates = [Predicate("amount", ">", 450.0), Predicate("label", "==", "red")]
        result, stats = reader.scan(predicates=predicates)

        expected = [
            row for row in rows
            if row["amount"] > 450.0 and row["label"] == "red"
        ]
        result_sorted = sorted(result, key=lambda r: r["id"])
        expected_sorted = sorted(expected, key=lambda r: r["id"])
        self.assertEqual(result_sorted, expected_sorted)

    def test_zone_map_skips_non_matching_row_groups(self):
        schema = Schema({"id": "int"})
        rows = [{"id": i} for i in range(5000)]
        reader = write_and_read(schema, rows, row_group_size=500)
        predicates = [Predicate("id", ">", 4900)]
        result, stats = reader.scan(predicates=predicates)
        self.assertEqual(sorted(r["id"] for r in result), list(range(4901, 5000)))
        self.assertGreater(stats.row_groups_skipped, 0)
        self.assertEqual(stats.row_groups_skipped + stats.row_groups_scanned, len(reader.row_groups))

    def test_zone_map_skipping_never_drops_matches_random(self):
        schema = Schema({"id": "int", "score": "int", "label": "str"})
        rng = random.Random(99)
        rows = []
        for i in range(4000):
            rows.append({
                "id": i,
                "score": rng.randint(0, 1000),
                "label": rng.choice(["a", "b", "c", "d"]),
            })
        reader = write_and_read(schema, rows, row_group_size=137)

        for _ in range(5):
            low = rng.randint(0, 900)
            high = low + rng.randint(1, 100)
            predicates = [Predicate("score", "between", low=low, high=high)]
            result, stats = reader.scan(predicates=predicates)
            expected = [row for row in rows if low <= row["score"] <= high]
            self.assertEqual(
                sorted(r["id"] for r in result),
                sorted(r["id"] for r in expected),
            )

    def test_between_and_equality_combo(self):
        schema = self.make_schema()
        rows = self.make_rows(1000)
        reader = write_and_read(schema, rows, row_group_size=100)
        predicates = [Predicate("id", "between", low=100, high=200), Predicate("active", "==", True)]
        result, _ = reader.scan(predicates=predicates)
        expected = [r for r in rows if 100 <= r["id"] <= 200 and r["active"] is True]
        self.assertEqual(
            sorted(result, key=lambda r: r["id"]),
            sorted(expected, key=lambda r: r["id"]),
        )

    def test_empty_dataset(self):
        schema = self.make_schema()
        reader = write_and_read(schema, [])
        self.assertEqual(reader.num_rows, 0)
        result, stats = reader.full_scan()
        self.assertEqual(result, [])
        self.assertEqual(stats.bytes_read, 0)

    def test_single_row(self):
        schema = self.make_schema()
        rows = [{"id": 1, "amount": 9.5, "label": "solo", "active": True}]
        reader = write_and_read(schema, rows)
        result, _ = reader.full_scan()
        self.assertEqual(result, rows)

    def test_all_null_column(self):
        schema = Schema({"id": "int", "note": "str"})
        rows = [{"id": i, "note": None} for i in range(50)]
        reader = write_and_read(schema, rows, row_group_size=10)
        result, stats = reader.full_scan()
        self.assertEqual(result, rows)
        for row_group in reader.row_groups:
            note_meta = row_group["columns"]["note"]
            self.assertEqual(note_meta["null_count"], row_group["num_rows"])
            self.assertIsNone(note_meta["min"])
            self.assertIsNone(note_meta["max"])

    def test_predicate_skips_all_null_chunk(self):
        schema = Schema({"id": "int", "note": "str"})
        rows = [{"id": i, "note": None if i < 500 else "x"} for i in range(1000)]
        reader = write_and_read(schema, rows, row_group_size=100)
        predicates = [Predicate("note", "==", "y")]
        result, stats = reader.scan(predicates=predicates)
        self.assertEqual(result, [])
        self.assertEqual(stats.row_groups_skipped, len(reader.row_groups))

    def test_mixed_nulls_round_trip(self):
        schema = Schema({"id": "int", "value": "float"})
        rows = []
        for i in range(200):
            rows.append({"id": i, "value": None if i % 7 == 0 else float(i)})
        reader = write_and_read(schema, rows, row_group_size=30)
        result, _ = reader.full_scan()
        self.assertEqual(result, rows)


if __name__ == "__main__":
    unittest.main()
