import struct
from typing import List

from .bitio import BitReader, BitWriter

TIMESTAMP_BITS = 64
LEADING_ZERO_BITS = 5
LENGTH_FIELD_BITS = 6
VALUE_BITS = 64
MAX_LEADING = (1 << LEADING_ZERO_BITS) - 1


def _float_to_bits(value: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", value))[0]


def _bits_to_float(bits: int) -> float:
    return struct.unpack(">d", struct.pack(">Q", bits))[0]


def _leading_zeros_64(x: int) -> int:
    if x == 0:
        return 64
    return 64 - x.bit_length()


def _trailing_zeros_64(x: int) -> int:
    if x == 0:
        return 64
    n = 0
    while (x & 1) == 0:
        x >>= 1
        n += 1
    return n


def encode_timestamps(timestamps: List[int]) -> bytes:
    if not timestamps:
        return b""
    bw = BitWriter()
    bw.write_bits(timestamps[0] & ((1 << TIMESTAMP_BITS) - 1), TIMESTAMP_BITS)
    if len(timestamps) == 1:
        return bw.getvalue()
    prev_delta = timestamps[1] - timestamps[0]
    bw.write_int(prev_delta, TIMESTAMP_BITS)
    prev_ts = timestamps[1]
    for ts in timestamps[2:]:
        delta = ts - prev_ts
        dod = delta - prev_delta
        if dod == 0:
            bw.write_bit(0)
        elif -64 <= dod <= 63:
            bw.write_bits(0b10, 2)
            bw.write_int(dod, 7)
        elif -256 <= dod <= 255:
            bw.write_bits(0b110, 3)
            bw.write_int(dod, 9)
        elif -2048 <= dod <= 2047:
            bw.write_bits(0b1110, 4)
            bw.write_int(dod, 12)
        else:
            bw.write_bits(0b1111, 4)
            bw.write_int(dod, TIMESTAMP_BITS)
        prev_delta = delta
        prev_ts = ts
    return bw.getvalue()


def decode_timestamps(data: bytes, count: int) -> List[int]:
    if count == 0:
        return []
    br = BitReader(data)
    t0 = br.read_bits(TIMESTAMP_BITS)
    result = [t0]
    if count == 1:
        return result
    delta = br.read_int(TIMESTAMP_BITS)
    t1 = t0 + delta
    result.append(t1)
    prev_delta = delta
    prev_ts = t1
    for _ in range(count - 2):
        if br.read_bit() == 0:
            dod = 0
        elif br.read_bit() == 0:
            dod = br.read_int(7)
        elif br.read_bit() == 0:
            dod = br.read_int(9)
        elif br.read_bit() == 0:
            dod = br.read_int(12)
        else:
            dod = br.read_int(TIMESTAMP_BITS)
        delta = prev_delta + dod
        ts = prev_ts + delta
        result.append(ts)
        prev_delta = delta
        prev_ts = ts
    return result


def encode_values(values: List[float]) -> bytes:
    if not values:
        return b""
    bw = BitWriter()
    prev_bits = _float_to_bits(values[0])
    bw.write_bits(prev_bits, VALUE_BITS)
    prev_leading = MAX_LEADING + 1
    prev_trailing = 0
    for value in values[1:]:
        bits = _float_to_bits(value)
        xor = bits ^ prev_bits
        if xor == 0:
            bw.write_bit(0)
        else:
            bw.write_bit(1)
            leading = _leading_zeros_64(xor)
            trailing = _trailing_zeros_64(xor)
            leading_clamped = min(leading, MAX_LEADING)
            if leading_clamped >= prev_leading and trailing >= prev_trailing:
                bw.write_bit(0)
                meaningful_bits = VALUE_BITS - prev_leading - prev_trailing
                block = (xor >> prev_trailing) & ((1 << meaningful_bits) - 1)
                bw.write_bits(block, meaningful_bits)
            else:
                bw.write_bit(1)
                bw.write_bits(leading_clamped, LEADING_ZERO_BITS)
                meaningful_bits = VALUE_BITS - leading_clamped - trailing
                bw.write_bits(meaningful_bits - 1, LENGTH_FIELD_BITS)
                block = (xor >> trailing) & ((1 << meaningful_bits) - 1)
                bw.write_bits(block, meaningful_bits)
                prev_leading = leading_clamped
                prev_trailing = trailing
        prev_bits = bits
    return bw.getvalue()


def decode_values(data: bytes, count: int) -> List[float]:
    if count == 0:
        return []
    br = BitReader(data)
    prev_bits = br.read_bits(VALUE_BITS)
    result = [_bits_to_float(prev_bits)]
    prev_leading = 0
    prev_trailing = 0
    for _ in range(count - 1):
        if br.read_bit() == 0:
            bits = prev_bits
        else:
            if br.read_bit() == 0:
                meaningful_bits = VALUE_BITS - prev_leading - prev_trailing
                block = br.read_bits(meaningful_bits)
                xor = block << prev_trailing
            else:
                leading = br.read_bits(LEADING_ZERO_BITS)
                meaningful_bits = br.read_bits(LENGTH_FIELD_BITS) + 1
                trailing = VALUE_BITS - leading - meaningful_bits
                block = br.read_bits(meaningful_bits)
                xor = block << trailing
                prev_leading = leading
                prev_trailing = trailing
            bits = prev_bits ^ xor
        result.append(_bits_to_float(bits))
        prev_bits = bits
    return result
