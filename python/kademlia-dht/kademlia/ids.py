from __future__ import annotations

import hashlib
import random

DEFAULT_BITS = 160


def random_id(rng: random.Random, bits: int = DEFAULT_BITS) -> int:
    return rng.getrandbits(bits)


def key_id(key: str, bits: int = DEFAULT_BITS) -> int:
    digest = hashlib.sha1(key.encode("utf-8")).digest()
    value = int.from_bytes(digest, "big")
    if bits >= 160:
        return value << (bits - 160)
    return value >> (160 - bits)


def distance(a: int, b: int) -> int:
    return a ^ b


def shared_prefix_len(a: int, b: int, bits: int = DEFAULT_BITS) -> int:
    d = distance(a, b)
    if d == 0:
        return bits
    return bits - d.bit_length()


def bucket_index(owner_id: int, other_id: int, bits: int = DEFAULT_BITS) -> int:
    d = distance(owner_id, other_id)
    if d == 0:
        raise ValueError("a node has no bucket for its own id")
    if d.bit_length() > bits:
        raise ValueError("id does not fit in the configured key space")
    return d.bit_length() - 1


def id_in_bucket(owner_id: int, index: int, rng: random.Random) -> int:
    offset = (1 << index) | (rng.getrandbits(index) if index else 0)
    return owner_id ^ offset


def short_id(node_id: int, bits: int = DEFAULT_BITS, width: int = 8) -> str:
    full = format(node_id, "0{}x".format((bits + 3) // 4))
    return full[:width]
