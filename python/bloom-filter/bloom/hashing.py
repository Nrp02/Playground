from __future__ import annotations

import hashlib


def base_hashes(item: str) -> tuple[int, int]:
    encoded = item.encode("utf-8")
    digest_a = hashlib.md5(encoded).digest()
    digest_b = hashlib.sha1(encoded).digest()
    h1 = int.from_bytes(digest_a[:8], "big")
    h2 = int.from_bytes(digest_b[:8], "big")
    if h2 % 2 == 0:
        h2 += 1
    return h1, h2


def nth_hash(item: str, index: int, modulus: int) -> int:
    h1, h2 = base_hashes(item)
    return (h1 + index * h2) % modulus
