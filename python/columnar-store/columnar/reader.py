import json
import os
import struct
from typing import Any, Dict, List, Optional, Tuple

from columnar.encodings import DECODERS, unpack_bitmap
from columnar.predicates import Predicate
from columnar.schema import Schema
from columnar.writer import CHUNK_HEADER_FMT, CHUNK_HEADER_SIZE

TRAILER_SIZE = 8


class ScanStats:
    def __init__(self) -> None:
        self.bytes_read = 0
        self.row_groups_scanned = 0
        self.row_groups_skipped = 0

    def as_dict(self) -> Dict[str, int]:
        return {
            "bytes_read": self.bytes_read,
            "row_groups_scanned": self.row_groups_scanned,
            "row_groups_skipped": self.row_groups_skipped,
        }


class ColumnarReader:
    def __init__(self, path: str) -> None:
        self.path = path
        file_size = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(file_size - TRAILER_SIZE)
            footer_offset = struct.unpack("<Q", f.read(TRAILER_SIZE))[0]
            f.seek(footer_offset)
            footer_bytes = f.read(file_size - TRAILER_SIZE - footer_offset)
        footer = json.loads(footer_bytes.decode("utf-8"))
        self.schema = Schema.from_dict(footer["schema"])
        self.row_groups: List[Dict[str, Any]] = footer["row_groups"]
        self.num_rows: int = footer["num_rows"]

    def _read_chunk(self, f: Any, column_meta: Dict[str, Any], ctype: str, stats: ScanStats) -> List[Any]:
        offset = column_meta["offset"]
        length = column_meta["length"]
        f.seek(offset)
        data = f.read(length)
        stats.bytes_read += length
        code, num_rows, null_count = struct.unpack_from(CHUNK_HEADER_FMT, data, 0)
        pos = CHUNK_HEADER_SIZE
        bitmap_size = (num_rows + 7) // 8
        bitmap = data[pos:pos + bitmap_size]
        pos += bitmap_size
        payload = data[pos:]
        null_mask = unpack_bitmap(bitmap, num_rows)
        non_null_count = num_rows - null_count
        decoded = DECODERS[code](ctype, payload, non_null_count) if non_null_count else []
        values: List[Any] = []
        it = iter(decoded)
        for is_null in null_mask:
            values.append(None if is_null else next(it))
        return values

    def scan(
        self,
        columns: Optional[List[str]] = None,
        predicates: Optional[List[Predicate]] = None,
    ) -> Tuple[List[Dict[str, Any]], ScanStats]:
        predicates = predicates or []
        projected = columns if columns is not None else self.schema.names()
        needed = set(projected) | {p.column for p in predicates}
        stats = ScanStats()
        result: List[Dict[str, Any]] = []
        with open(self.path, "rb") as f:
            for row_group in self.row_groups:
                if row_group["num_rows"] == 0:
                    continue
                skip = False
                for predicate in predicates:
                    column_meta = row_group["columns"][predicate.column]
                    if predicate.can_skip_zone(
                        column_meta["min"], column_meta["max"],
                        column_meta["null_count"], column_meta["num_rows"],
                    ):
                        skip = True
                        break
                if skip:
                    stats.row_groups_skipped += 1
                    continue
                stats.row_groups_scanned += 1
                decoded_columns: Dict[str, List[Any]] = {}
                for name in needed:
                    ctype = self.schema.type_of(name)
                    column_meta = row_group["columns"][name]
                    decoded_columns[name] = self._read_chunk(f, column_meta, ctype, stats)
                for i in range(row_group["num_rows"]):
                    ok = True
                    for predicate in predicates:
                        if not predicate.matches(decoded_columns[predicate.column][i]):
                            ok = False
                            break
                    if not ok:
                        continue
                    result.append({name: decoded_columns[name][i] for name in projected})
        return result, stats

    def full_scan(self, columns: Optional[List[str]] = None) -> Tuple[List[Dict[str, Any]], ScanStats]:
        return self.scan(columns=columns, predicates=None)
