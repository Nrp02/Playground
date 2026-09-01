from typing import Dict, List, Sequence

from . import gf256, matrix


def build_generator_matrix(k: int, m: int) -> matrix.Matrix:
    if k <= 0:
        raise ValueError("k must be positive")
    if m <= 0:
        raise ValueError("m must be positive")
    if k + m > 255:
        raise ValueError("k + m must not exceed 255")
    top = matrix.identity(k)
    bottom = []
    for i in range(m):
        x = k + i
        row = [gf256.inv(x ^ j) for j in range(k)]
        bottom.append(row)
    return top + bottom


def xor_bytes(a: bytes, b: bytes) -> bytes:
    if len(a) != len(b):
        raise ValueError("shard length mismatch")
    n = len(a)
    value = int.from_bytes(a, "big") ^ int.from_bytes(b, "big")
    return value.to_bytes(n, "big")


def combine_row(coefficients: Sequence[int], shards: Sequence[bytes], shard_len: int) -> bytes:
    result = bytes(shard_len)
    for coefficient, shard in zip(coefficients, shards):
        if coefficient == 0:
            continue
        table = gf256.mul_table(coefficient)
        term = shard.translate(table)
        result = xor_bytes(result, term)
    return result


class ReedSolomon:
    def __init__(self, k: int, m: int) -> None:
        self.k = k
        self.m = m
        self.matrix = build_generator_matrix(k, m)

    def encode(self, data_shards: Sequence[bytes]) -> List[bytes]:
        if len(data_shards) != self.k:
            raise ValueError(f"expected {self.k} data shards, got {len(data_shards)}")
        shard_len = len(data_shards[0])
        for shard in data_shards:
            if len(shard) != shard_len:
                raise ValueError("all data shards must be the same length")
        shards: List[bytes] = list(data_shards)
        for i in range(self.m):
            row = self.matrix[self.k + i]
            shards.append(combine_row(row, data_shards, shard_len))
        return shards

    def reconstruct(self, available: Dict[int, bytes]) -> List[bytes]:
        if len(available) < self.k:
            raise ValueError(
                f"cannot reconstruct: need at least {self.k} shards, have {len(available)}"
            )
        chosen_indices = sorted(available.keys())[: self.k]
        shard_len = len(available[chosen_indices[0]])
        for idx in chosen_indices:
            if len(available[idx]) != shard_len:
                raise ValueError("all shards must be the same length")

        sub_matrix = matrix.submatrix_rows(self.matrix, chosen_indices)
        inverse = matrix.invert(sub_matrix)

        chosen_shards = [available[idx] for idx in chosen_indices]
        recovered_data: List[bytes] = []
        for row in inverse:
            recovered_data.append(combine_row(row, chosen_shards, shard_len))
        return recovered_data

    def reconstruct_all(self, available: Dict[int, bytes]) -> List[bytes]:
        data_shards = self.reconstruct(available)
        full = self.encode(data_shards)
        return full
