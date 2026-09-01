import hashlib
from dataclasses import dataclass
from typing import Dict, List, Sequence, Tuple

from .reedsolomon import ReedSolomon


@dataclass(frozen=True)
class Shard:
    index: int
    data: bytes
    checksum: bytes


def compute_checksum(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def verify_checksum(shard: Shard) -> bool:
    return compute_checksum(shard.data) == shard.checksum


def split_file(payload: bytes, k: int, m: int) -> Tuple[List[Shard], int]:
    rs = ReedSolomon(k, m)
    original_length = len(payload)
    shard_len = -(-original_length // k) if original_length > 0 else 0
    padded_length = shard_len * k
    padded = payload + bytes(padded_length - original_length)
    data_shards = [padded[i * shard_len:(i + 1) * shard_len] for i in range(k)]
    all_shards = rs.encode(data_shards)
    shards = [Shard(i, shard, compute_checksum(shard)) for i, shard in enumerate(all_shards)]
    return shards, original_length


def verified_shard_map(shards: Sequence[Shard]) -> Dict[int, bytes]:
    return {shard.index: shard.data for shard in shards if verify_checksum(shard)}


def reconstruct_file(rs: ReedSolomon, shards: Sequence[Shard], original_length: int) -> bytes:
    available = verified_shard_map(shards)
    data_shards = rs.reconstruct(available)
    payload = b"".join(data_shards)
    return payload[:original_length]
