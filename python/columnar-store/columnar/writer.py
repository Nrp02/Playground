import json
import struct
from typing import Any, Dict, List

from columnar.encodings import ENCODERS, PLAIN, choose_encoding, pack_bitmap
from columnar.schema import Schema

DEFAULT_ROW_GROUP_SIZE = 4096
CHUNK_HEADER_FMT = "<BII"
CHUNK_HEADER_SIZE = struct.calcsize(CHUNK_HEADER_FMT)


def write(path: str, schema: Schema, rows: List[Dict[str, Any]], row_group_size: int = DEFAULT_ROW_GROUP_SIZE) -> None:
    row_groups_meta: List[Dict[str, Any]] = []
    with open(path, "wb") as f:
        for start in range(0, len(rows), row_group_size):
            group_rows = rows[start:start + row_group_size]
            columns_meta: Dict[str, Any] = {}
            for name in schema.names():
                ctype = schema.type_of(name)
                raw_values = [row.get(name) for row in group_rows]
                null_mask = [value is None for value in raw_values]
                non_null_values = [value for value in raw_values if value is not None]
                null_count = sum(1 for is_null in null_mask if is_null)
                if non_null_values:
                    code = choose_encoding(ctype, non_null_values)
                    payload = ENCODERS[code](ctype, non_null_values)
                    min_value = min(non_null_values)
                    max_value = max(non_null_values)
                else:
                    code = PLAIN
                    payload = b""
                    min_value = None
                    max_value = None
                bitmap = pack_bitmap(null_mask)
                offset = f.tell()
                f.write(struct.pack(CHUNK_HEADER_FMT, code, len(group_rows), null_count))
                f.write(bitmap)
                f.write(payload)
                length = f.tell() - offset
                columns_meta[name] = {
                    "offset": offset,
                    "length": length,
                    "encoding": code,
                    "num_rows": len(group_rows),
                    "null_count": null_count,
                    "min": min_value,
                    "max": max_value,
                }
            row_groups_meta.append({"num_rows": len(group_rows), "columns": columns_meta})
        footer = {"schema": schema.to_dict(), "row_groups": row_groups_meta, "num_rows": len(rows)}
        footer_bytes = json.dumps(footer).encode("utf-8")
        footer_offset = f.tell()
        f.write(footer_bytes)
        f.write(struct.pack("<Q", footer_offset))
