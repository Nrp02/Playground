import os
import sys

from db import Database
from parser import parse_script
from engine import execute


def print_table(headers, rows):
    if not rows:
        print("(0 rows)")
        return
    widths = [len(h) for h in headers]
    for row in rows:
        for i, val in enumerate(row):
            widths[i] = max(widths[i], len(str(val)))

    def fmt_row(vals):
        return " | ".join(str(v).ljust(widths[i]) for i, v in enumerate(vals))

    print(fmt_row(headers))
    print("-+-".join("-" * w for w in widths))
    for row in rows:
        print(fmt_row(row))


def main():
    if len(sys.argv) < 2:
        print("usage: python3 main.py <script.sql> [csv files...]", file=sys.stderr)
        sys.exit(1)

    script_path = sys.argv[1]
    csv_paths = sys.argv[2:]

    db = Database()
    for path in csv_paths:
        name = os.path.splitext(os.path.basename(path))[0]
        db.load_csv(name, path)

    with open(script_path) as f:
        text = f.read()

    statements = parse_script(text)
    for i, stmt in enumerate(statements):
        headers, rows = execute(stmt, db)
        print(f"-- query {i + 1} --")
        print_table(headers, rows)
        print()


if __name__ == "__main__":
    main()
