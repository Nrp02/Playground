from typing import List


def to_nibbles(data: bytes) -> List[int]:
    nibbles: List[int] = []
    for byte in data:
        nibbles.append(byte >> 4)
        nibbles.append(byte & 0x0F)
    return nibbles


def common_prefix_length(a: List[int], b: List[int]) -> int:
    n = 0
    limit = min(len(a), len(b))
    while n < limit and a[n] == b[n]:
        n += 1
    return n
