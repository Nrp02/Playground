import os
import random
import sys

from erasure.fileapi import Shard, reconstruct_file, split_file, verify_checksum
from erasure.reedsolomon import ReedSolomon


def main() -> int:
    rng = random.Random(1234)
    k, m = 6, 3
    payload_size = 300_000
    payload = bytes(rng.getrandbits(8) for _ in range(payload_size))

    print(f"=== Reed-Solomon erasure coding demo (k={k}, m={m}) ===")
    shards, original_length = split_file(payload, k, m)
    shard_len = len(shards[0].data)
    print(f"payload size: {original_length} bytes")
    print(f"shard size:   {shard_len} bytes each, {k} data + {m} parity = {k + m} shards")

    total_erasure_bytes = shard_len * (k + m)
    replication_copies_needed = m + 1
    total_replication_bytes = original_length * replication_copies_needed
    erasure_overhead = total_erasure_bytes / original_length
    replication_overhead = total_replication_bytes / original_length
    print(f"erasure coding storage overhead:  {erasure_overhead:.2f}x original size")
    print(
        f"replication storage overhead:     {replication_overhead:.2f}x original size "
        f"({replication_copies_needed} copies needed to tolerate losing any {m})"
    )
    print(f"savings from erasure coding:       {replication_overhead / erasure_overhead:.2f}x less storage\n")

    rs = ReedSolomon(k, m)

    print(f"=== losing {m} arbitrary shards, reconstructing ===")
    dropped = sorted(rng.sample(range(k + m), m))
    surviving = [s for s in shards if s.index not in dropped]
    print(f"dropped shard indices: {dropped}")
    recovered = reconstruct_file(rs, surviving, original_length)
    print(f"reconstruction matches original: {recovered == payload}\n")

    print(f"=== losing {m + 1} shards (one more than tolerable) ===")
    dropped_too_many = sorted(rng.sample(range(k + m), m + 1))
    surviving_too_few = [s for s in shards if s.index not in dropped_too_many]
    print(f"dropped shard indices: {dropped_too_many}")
    try:
        reconstruct_file(rs, surviving_too_few, original_length)
        print("ERROR: reconstruction should have failed but did not")
    except ValueError as exc:
        print(f"reconstruction failed cleanly as expected: {exc}\n")

    print("=== silent corruption without checksum protection ===")
    victim_index = 2
    victim = shards[victim_index]
    corrupted_bytes = bytearray(victim.data)
    corrupted_bytes[0] ^= 0xFF
    corrupted_shard_raw = bytes(corrupted_bytes)
    naive_available = {s.index: s.data for s in shards}
    naive_available[victim_index] = corrupted_shard_raw
    naive_recovered_shards = rs.reconstruct(naive_available)
    naive_payload = b"".join(naive_recovered_shards)[:original_length]
    print(f"reconstruction without checksum check matches original: {naive_payload == payload}")
    print("(a corrupted shard was silently trusted and treated as valid data, producing garbage)\n")

    print("=== the same corruption, caught by the per-shard checksum ===")
    corrupted_shard = Shard(index=victim_index, data=corrupted_shard_raw, checksum=victim.checksum)
    print(f"checksum still valid for corrupted bytes: {verify_checksum(corrupted_shard)} (detected as corrupted)")
    shards_with_corruption = list(shards)
    shards_with_corruption[victim_index] = corrupted_shard
    recovered_via_checksum = reconstruct_file(rs, shards_with_corruption, original_length)
    print(f"corrupted shard treated as erasure, reconstruction matches original: {recovered_via_checksum == payload}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
