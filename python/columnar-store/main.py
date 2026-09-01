import random
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, List

from columnar.predicates import Predicate
from columnar.reader import ColumnarReader
from columnar.schema import Schema
from columnar.writer import write

CATEGORIES = ["A", "B", "C", "D", "E"]


def build_rows(n: int) -> List[Dict[str, Any]]:
    rng = random.Random(42)
    rows = []
    for i in range(n):
        status = "active" if (i // 500) % 2 == 0 else "inactive"
        rows.append({
            "id": i,
            "value": rng.uniform(0.0, 1000.0),
            "category": rng.choice(CATEGORIES),
            "status": status,
            "flag": rng.random() < 0.5,
            "score": rng.randint(0, 9),
        })
    return rows


def naive_row_bytes(schema: Schema, rows: List[Dict[str, Any]]) -> int:
    total = 0
    for row in rows:
        for name in schema.names():
            ctype = schema.type_of(name)
            value = row.get(name)
            if value is None:
                total += 1
                continue
            if ctype == "int":
                total += 8
            elif ctype == "float":
                total += 8
            elif ctype == "bool":
                total += 1
            elif ctype == "str":
                total += 4 + len(str(value).encode("utf-8"))
    return total


def report(label: str, rows_returned: int, stats: Any, naive_bytes: int) -> None:
    print(f"--- {label} ---")
    print(f"  rows returned:        {rows_returned}")
    print(f"  columnar bytes read:  {stats.bytes_read}")
    print(f"  row groups scanned:   {stats.row_groups_scanned}")
    print(f"  row groups skipped:   {stats.row_groups_skipped}")
    print(f"  naive row-store bytes:{naive_bytes}")
    if stats.bytes_read:
        print(f"  columnar win:         {naive_bytes / stats.bytes_read:.2f}x fewer bytes read")
    print()


def main() -> int:
    schema = Schema({
        "id": "int",
        "value": "float",
        "category": "str",
        "status": "str",
        "flag": "bool",
        "score": "int",
    })
    n = 50000
    rows = build_rows(n)

    with tempfile.TemporaryDirectory() as tmp:
        path = str(Path(tmp) / "data.columnar")
        write(path, schema, rows, row_group_size=4096)
        reader = ColumnarReader(path)

        print(f"wrote {n} rows across {len(reader.row_groups)} row groups")
        for name in schema.names():
            encoding = reader.row_groups[0]["columns"][name]["encoding"]
            print(f"  column '{name}' ({schema.type_of(name)}) -> encoding code {encoding}")
        print()

        naive_full = naive_row_bytes(schema, rows)

        result, stats = reader.full_scan()
        report("full scan (all columns)", len(result), stats, naive_full)

        result, stats = reader.scan(columns=["id", "category"])
        report("projection scan (2 of 6 columns)", len(result), stats, naive_full)

        threshold = int(n * 0.95)
        predicates = [Predicate("id", ">", threshold), Predicate("category", "==", "A")]
        result, stats = reader.scan(columns=["id", "value"], predicates=predicates)
        report("filtered scan (id > 95th pct AND category == 'A')", len(result), stats, naive_full)

    return 0


if __name__ == "__main__":
    sys.exit(main())
