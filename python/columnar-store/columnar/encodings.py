import struct
from typing import Any, List, Tuple

PLAIN = 0
DICTIONARY = 1
RLE = 2
BITPACK = 3

ENCODING_NAMES = {PLAIN: "PLAIN", DICTIONARY: "DICTIONARY", RLE: "RLE", BITPACK: "BITPACK"}


def bits_needed(n: int) -> int:
    if n <= 0:
        return 1
    return max(1, n.bit_length())


def pack_bitmap(mask: List[bool]) -> bytes:
    nbytes = (len(mask) + 7) // 8
    out = bytearray(nbytes)
    for i, is_null in enumerate(mask):
        if is_null:
            out[i // 8] |= 1 << (i % 8)
    return bytes(out)


def unpack_bitmap(data: bytes, n: int) -> List[bool]:
    return [((data[i // 8] >> (i % 8)) & 1) == 1 for i in range(n)]


def pack_value(ctype: str, value: Any) -> bytes:
    if ctype == "int":
        return struct.pack("<q", value)
    if ctype == "float":
        return struct.pack("<d", value)
    if ctype == "bool":
        return struct.pack("<B", 1 if value else 0)
    if ctype == "str":
        encoded = value.encode("utf-8")
        return struct.pack("<I", len(encoded)) + encoded
    raise ValueError(f"unsupported type: {ctype}")


def unpack_value(ctype: str, data: bytes, offset: int) -> Tuple[Any, int]:
    if ctype == "int":
        return struct.unpack_from("<q", data, offset)[0], offset + 8
    if ctype == "float":
        return struct.unpack_from("<d", data, offset)[0], offset + 8
    if ctype == "bool":
        return struct.unpack_from("<B", data, offset)[0] == 1, offset + 1
    if ctype == "str":
        length = struct.unpack_from("<I", data, offset)[0]
        start = offset + 4
        end = start + length
        return data[start:end].decode("utf-8"), end
    raise ValueError(f"unsupported type: {ctype}")


def encode_plain(ctype: str, values: List[Any]) -> bytes:
    out = bytearray()
    for value in values:
        out += pack_value(ctype, value)
    return bytes(out)


def decode_plain(ctype: str, data: bytes, num_values: int) -> List[Any]:
    values = []
    offset = 0
    for _ in range(num_values):
        value, offset = unpack_value(ctype, data, offset)
        values.append(value)
    return values


def count_runs(values: List[Any]) -> int:
    if not values:
        return 0
    runs = 1
    for i in range(1, len(values)):
        if values[i] != values[i - 1]:
            runs += 1
    return runs


def encode_rle(ctype: str, values: List[Any]) -> bytes:
    runs: List[Tuple[Any, int]] = []
    for value in values:
        if runs and runs[-1][0] == value:
            runs[-1] = (value, runs[-1][1] + 1)
        else:
            runs.append((value, 1))
    out = bytearray()
    out += struct.pack("<I", len(runs))
    for value, run_length in runs:
        out += pack_value(ctype, value)
        out += struct.pack("<I", run_length)
    return bytes(out)


def decode_rle(ctype: str, data: bytes, num_values: int) -> List[Any]:
    num_runs = struct.unpack_from("<I", data, 0)[0]
    offset = 4
    values: List[Any] = []
    for _ in range(num_runs):
        value, offset = unpack_value(ctype, data, offset)
        run_length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        values.extend([value] * run_length)
    return values


def encode_dictionary(ctype: str, values: List[Any]) -> bytes:
    uniques: List[Any] = []
    index_of = {}
    for value in values:
        if value not in index_of:
            index_of[value] = len(uniques)
            uniques.append(value)
    uniques_sorted = sorted(uniques, key=lambda v: (str(type(v)), v))
    remap = {old_val: new_idx for new_idx, old_val in enumerate(uniques_sorted)}
    num_uniques = len(uniques_sorted)
    if num_uniques <= 255:
        index_width = 1
        index_fmt = "<B"
    elif num_uniques <= 65535:
        index_width = 2
        index_fmt = "<H"
    else:
        index_width = 4
        index_fmt = "<I"
    out = bytearray()
    out += struct.pack("<IB", num_uniques, index_width)
    out += encode_plain(ctype, uniques_sorted)
    for value in values:
        out += struct.pack(index_fmt, remap[value])
    return bytes(out)


def decode_dictionary(ctype: str, data: bytes, num_values: int) -> List[Any]:
    num_uniques, index_width = struct.unpack_from("<IB", data, 0)
    offset = 5
    uniques: List[Any] = []
    for _ in range(num_uniques):
        value, offset = unpack_value(ctype, data, offset)
        uniques.append(value)
    index_fmt = {1: "<B", 2: "<H", 4: "<I"}[index_width]
    values = []
    for _ in range(num_values):
        idx = struct.unpack_from(index_fmt, data, offset)[0]
        offset += index_width
        values.append(uniques[idx])
    return values


def encode_bitpack(ctype: str, values: List[int]) -> bytes:
    min_value = min(values)
    max_value = max(values)
    width = bits_needed(max_value - min_value)
    header = struct.pack("<qB", min_value, width)
    buffer_bits = 0
    bits_in_buffer = 0
    data = bytearray()
    for value in values:
        shifted = value - min_value
        buffer_bits |= shifted << bits_in_buffer
        bits_in_buffer += width
        while bits_in_buffer >= 8:
            data.append(buffer_bits & 0xFF)
            buffer_bits >>= 8
            bits_in_buffer -= 8
    if bits_in_buffer > 0:
        data.append(buffer_bits & 0xFF)
    return header + bytes(data)


def decode_bitpack(ctype: str, data: bytes, num_values: int) -> List[int]:
    min_value, width = struct.unpack_from("<qB", data, 0)
    offset = 9
    buffer_bits = 0
    bits_in_buffer = 0
    mask = (1 << width) - 1
    values = []
    for _ in range(num_values):
        while bits_in_buffer < width:
            buffer_bits |= data[offset] << bits_in_buffer
            offset += 1
            bits_in_buffer += 8
        raw = buffer_bits & mask
        buffer_bits >>= width
        bits_in_buffer -= width
        values.append(raw + min_value)
    return values


ENCODERS = {
    PLAIN: encode_plain,
    DICTIONARY: encode_dictionary,
    RLE: encode_rle,
    BITPACK: encode_bitpack,
}

DECODERS = {
    PLAIN: decode_plain,
    DICTIONARY: decode_dictionary,
    RLE: decode_rle,
    BITPACK: decode_bitpack,
}


def choose_encoding(ctype: str, values: List[Any]) -> int:
    if not values:
        return PLAIN
    n = len(values)
    runs = count_runs(values)
    if runs <= max(1, n // 3):
        return RLE
    if ctype == "str":
        unique_count = len(set(values))
        if unique_count <= max(1, n // 2):
            return DICTIONARY
        return PLAIN
    if ctype == "int":
        value_range = max(values) - min(values)
        width = bits_needed(value_range)
        if width <= 32:
            return BITPACK
        return PLAIN
    return PLAIN
