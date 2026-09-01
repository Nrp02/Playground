import itertools
import random
import unittest

from erasure import gf256, matrix
from erasure.fileapi import Shard, compute_checksum, reconstruct_file, split_file, verify_checksum
from erasure.reedsolomon import ReedSolomon, build_generator_matrix


class TestGF256Field(unittest.TestCase):
    def test_multiplicative_identity(self) -> None:
        for a in range(256):
            self.assertEqual(gf256.mul(a, 1), a)

    def test_zero_annihilates(self) -> None:
        for a in range(256):
            self.assertEqual(gf256.mul(a, 0), 0)

    def test_inverse(self) -> None:
        for a in range(1, 256):
            self.assertEqual(gf256.mul(a, gf256.inv(a)), 1)

    def test_additive_self_inverse(self) -> None:
        for a in range(256):
            self.assertEqual(gf256.add(a, a), 0)

    def test_associativity(self) -> None:
        rng = random.Random(1)
        for _ in range(2000):
            a, b, c = rng.randrange(256), rng.randrange(256), rng.randrange(256)
            self.assertEqual(gf256.mul(gf256.mul(a, b), c), gf256.mul(a, gf256.mul(b, c)))

    def test_distributivity(self) -> None:
        rng = random.Random(2)
        for _ in range(2000):
            a, b, c = rng.randrange(256), rng.randrange(256), rng.randrange(256)
            lhs = gf256.mul(a, gf256.add(b, c))
            rhs = gf256.add(gf256.mul(a, b), gf256.mul(a, c))
            self.assertEqual(lhs, rhs)

    def test_division_matches_multiplication(self) -> None:
        rng = random.Random(3)
        for _ in range(500):
            a = rng.randrange(1, 256)
            b = rng.randrange(1, 256)
            self.assertEqual(gf256.div(gf256.mul(a, b), b), a)

    def test_division_by_zero_raises(self) -> None:
        with self.assertRaises(ZeroDivisionError):
            gf256.div(5, 0)

    def test_inverse_of_zero_raises(self) -> None:
        with self.assertRaises(ZeroDivisionError):
            gf256.inv(0)


class TestMatrix(unittest.TestCase):
    def test_identity_multiply(self) -> None:
        m = build_generator_matrix(4, 2)
        sub = matrix.submatrix_rows(m, [0, 1, 2, 3])
        self.assertEqual(sub, matrix.identity(4))

    def test_inversion_round_trip(self) -> None:
        rng = random.Random(4)
        for k, m_count in [(2, 2), (3, 3), (4, 4), (6, 5)]:
            gen = build_generator_matrix(k, m_count)
            for _ in range(20):
                indices = sorted(rng.sample(range(k + m_count), k))
                sub = matrix.submatrix_rows(gen, indices)
                inverse = matrix.invert(sub)
                product = matrix.multiply(sub, inverse)
                self.assertEqual(product, matrix.identity(k))

    def test_singular_matrix_raises(self) -> None:
        singular = [[1, 1], [1, 1]]
        with self.assertRaises(ValueError):
            matrix.invert(singular)


class TestReedSolomonExhaustive(unittest.TestCase):
    def setUp(self) -> None:
        self.rng = random.Random(5)

    def _random_shards(self, k: int, shard_len: int):
        return [bytes(self.rng.getrandbits(8) for _ in range(shard_len)) for _ in range(k)]

    def test_systematic_property(self) -> None:
        k, m = 4, 3
        rs = ReedSolomon(k, m)
        data_shards = self._random_shards(k, 16)
        all_shards = rs.encode(data_shards)
        self.assertEqual(all_shards[:k], data_shards)

    def test_exhaustive_loss_patterns_small_params(self) -> None:
        k, m = 3, 2
        rs = ReedSolomon(k, m)
        data_shards = self._random_shards(k, 20)
        all_shards = rs.encode(data_shards)
        total = k + m
        for num_lost in range(0, m + 1):
            for lost in itertools.combinations(range(total), num_lost):
                available = {i: all_shards[i] for i in range(total) if i not in lost}
                recovered = rs.reconstruct(available)
                self.assertEqual(recovered, data_shards, f"failed with lost={lost}")

    def test_exhaustive_parity_recovery_small_params(self) -> None:
        k, m = 3, 2
        rs = ReedSolomon(k, m)
        data_shards = self._random_shards(k, 20)
        all_shards = rs.encode(data_shards)
        total = k + m
        for lost in itertools.combinations(range(total), m):
            available = {i: all_shards[i] for i in range(total) if i not in lost}
            full = rs.reconstruct_all(available)
            self.assertEqual(full, all_shards, f"failed with lost={lost}")

    def test_randomized_loss_patterns_larger_params(self) -> None:
        k, m = 6, 3
        rs = ReedSolomon(k, m)
        data_shards = self._random_shards(k, 64)
        all_shards = rs.encode(data_shards)
        total = k + m
        for _ in range(30):
            lost = set(self.rng.sample(range(total), m))
            available = {i: all_shards[i] for i in range(total) if i not in lost}
            recovered = rs.reconstruct(available)
            self.assertEqual(recovered, data_shards)

    def test_more_than_m_losses_raises(self) -> None:
        k, m = 4, 2
        rs = ReedSolomon(k, m)
        data_shards = self._random_shards(k, 10)
        all_shards = rs.encode(data_shards)
        total = k + m
        available = {i: all_shards[i] for i in range(total) if i < k - 1}
        with self.assertRaises(ValueError):
            rs.reconstruct(available)

    def test_invalid_parameters(self) -> None:
        with self.assertRaises(ValueError):
            ReedSolomon(0, 2)
        with self.assertRaises(ValueError):
            ReedSolomon(4, 0)
        with self.assertRaises(ValueError):
            ReedSolomon(200, 100)


class TestFileAPI(unittest.TestCase):
    def setUp(self) -> None:
        self.rng = random.Random(6)

    def _round_trip(self, payload: bytes, k: int, m: int, drop_count: int) -> None:
        shards, original_length = split_file(payload, k, m)
        rs = ReedSolomon(k, m)
        total = k + m
        dropped = set(self.rng.sample(range(total), min(drop_count, total)))
        surviving = [s for s in shards if s.index not in dropped]
        recovered = reconstruct_file(rs, surviving, original_length)
        self.assertEqual(recovered, payload)

    def test_round_trip_not_multiple_of_shard_size(self) -> None:
        payload = bytes(self.rng.getrandbits(8) for _ in range(103))
        self._round_trip(payload, k=4, m=2, drop_count=2)

    def test_round_trip_empty_payload(self) -> None:
        self._round_trip(b"", k=4, m=2, drop_count=2)

    def test_round_trip_single_byte_payload(self) -> None:
        self._round_trip(bytes([0x42]), k=3, m=2, drop_count=2)

    def test_round_trip_large_payload(self) -> None:
        payload = bytes(self.rng.getrandbits(8) for _ in range(5000))
        self._round_trip(payload, k=6, m=3, drop_count=3)

    def test_checksum_detects_corruption(self) -> None:
        payload = bytes(self.rng.getrandbits(8) for _ in range(500))
        shards, _ = split_file(payload, k=4, m=2)
        victim = shards[1]
        self.assertTrue(verify_checksum(victim))
        corrupted_data = bytearray(victim.data)
        corrupted_data[0] ^= 0xFF
        corrupted_shard = Shard(index=victim.index, data=bytes(corrupted_data), checksum=victim.checksum)
        self.assertFalse(verify_checksum(corrupted_shard))

    def test_corrupted_shard_treated_as_erasure(self) -> None:
        payload = bytes(self.rng.getrandbits(8) for _ in range(500))
        shards, original_length = split_file(payload, k=4, m=2)
        rs = ReedSolomon(4, 2)
        victim = shards[1]
        corrupted_data = bytearray(victim.data)
        corrupted_data[0] ^= 0xFF
        corrupted_shard = Shard(index=victim.index, data=bytes(corrupted_data), checksum=victim.checksum)
        shards_with_corruption = list(shards)
        shards_with_corruption[victim.index] = corrupted_shard
        recovered = reconstruct_file(rs, shards_with_corruption, original_length)
        self.assertEqual(recovered, payload)

    def test_unflagged_corruption_produces_garbage(self) -> None:
        payload = bytes(self.rng.getrandbits(8) for _ in range(500))
        shards, original_length = split_file(payload, k=4, m=2)
        rs = ReedSolomon(4, 2)
        victim = shards[1]
        corrupted_data = bytearray(victim.data)
        corrupted_data[0] ^= 0xFF
        available = {s.index: s.data for s in shards}
        available[victim.index] = bytes(corrupted_data)
        recovered_shards = rs.reconstruct(available)
        recovered_payload = b"".join(recovered_shards)[:original_length]
        self.assertNotEqual(recovered_payload, payload)

    def test_checksum_helper_matches_sha256(self) -> None:
        data = b"hello world"
        checksum = compute_checksum(data)
        self.assertEqual(len(checksum), 32)
        self.assertEqual(compute_checksum(data), checksum)


if __name__ == "__main__":
    unittest.main()
