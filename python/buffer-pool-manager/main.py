from __future__ import annotations

import random
import struct
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

from bufferpool import (
    BufferPoolManager,
    DiskManager,
    PageStillPinnedError,
    PoolExhaustedError,
    RecordId,
    SlottedPage,
    TableHeap,
)

SEED = 20260904


def banner(title: str) -> None:
    print()
    print("=" * 74)
    print(title)
    print("=" * 74)


def payload_for(page_id: int) -> bytes:
    return f"page-{page_id:04d}-payload".encode("ascii")


def demo_page_lifecycle(directory: Path) -> None:
    banner("1. page lifecycle: a 3-frame pool holding a 6-page file")
    disk = DiskManager(directory / "lifecycle.db")
    pool = BufferPoolManager(disk, pool_size=3, policy="lru")

    page_ids: List[int] = []
    for _ in range(6):
        with pool.create_page() as page:
            page.write(payload_for(page.page_id))
            page_ids.append(page.page_id)

    print(f"created pages          : {page_ids}")
    print(f"resident after creation: {pool.resident_page_ids()} (pool holds {pool.pool_size})")
    print(f"evictions              : {pool.stats.evictions}")
    print(f"dirty write-backs      : {pool.stats.dirty_writebacks}")

    disk.reset_counters()
    pool.stats.reset()
    recovered = []
    for page_id in page_ids:
        with pool.read_page(page_id) as page:
            recovered.append(page.read(0, len(payload_for(page_id))).decode("ascii"))
    print(f"re-read after eviction : {recovered[0]} ... {recovered[-1]}")
    print(f"all payloads survived  : {recovered == [payload_for(p).decode('ascii') for p in page_ids]}")
    print(f"disk reads / writes    : {disk.reads} / {disk.writes}")
    print(f"hits / misses          : {pool.stats.hits} / {pool.stats.misses}")

    with pool.write_page(page_ids[0]) as page:
        page.write(b"rewritten", 0)
    print(f"dirty flag in pool     : page {page_ids[0]} -> {pool.is_dirty(page_ids[0])}")
    pool.flush_page(page_ids[0])
    print(f"after flush_page       : dirty={pool.is_dirty(page_ids[0])}, raw disk bytes={bytes(disk.read_page(page_ids[0])[:9])!r}")
    disk.close()


def demo_pinning(directory: Path) -> None:
    banner("2. pinning: a pinned page can never be evicted")
    disk = DiskManager(directory / "pinning.db")
    pool = BufferPoolManager(disk, pool_size=3, policy="clock")

    held = [pool.new_page() for _ in range(3)]
    for page in held:
        page.write(payload_for(page.page_id))
    print(f"pinned pages           : {pool.pinned_page_ids()}")
    print(f"free frames            : {pool.free_frames()}")

    try:
        pool.new_page()
        print("unexpectedly allocated a fourth frame")
    except PoolExhaustedError as exc:
        print(f"fourth allocation      : refused -> {exc}")

    try:
        pool.delete_page(held[0].page_id)
        print("unexpectedly deleted a pinned page")
    except PageStillPinnedError as exc:
        print(f"delete while pinned    : refused -> {exc}")

    victim = held[0].page_id
    held[0].unpin(is_dirty=True)
    print(f"unpinning one page     : page {victim} now has pin_count={pool.pin_count(victim)}")

    with pool.create_page() as fresh:
        fresh.write(b"took the freed frame")
        print(f"next allocation        : page {fresh.page_id} took the frame freed by evicting page {victim}")
    print(f"resident pages now     : {pool.resident_page_ids()}")
    print(f"evicted page on disk   : {bytes(disk.read_page(victim)[:len(payload_for(victim))])!r}")
    for page in held[1:]:
        page.unpin()
    disk.close()


def build_workload(rng: random.Random, hot_pages: List[int], cold_pages: List[int]) -> List[int]:
    accesses: List[int] = []
    cursor = 0
    for _ in range(40):
        for _ in range(30):
            accesses.append(rng.choice(hot_pages))
        for _ in range(20):
            accesses.append(cold_pages[cursor % len(cold_pages)])
            cursor += 1
    return accesses


def run_policy(directory: Path, policy: str, k: int, total_pages: int, accesses: List[int]) -> Tuple[float, int, int]:
    disk = DiskManager(directory / f"workload-{policy}-{k}.db")
    for _ in range(total_pages):
        disk.allocate_page()
    pool = BufferPoolManager(disk, pool_size=16, policy=policy, k=k)
    disk.reset_counters()
    for page_id in accesses:
        with pool.read_page(page_id):
            pass
    result = (pool.stats.hit_rate, pool.stats.evictions, disk.reads)
    disk.remove()
    return result


def demo_policies(directory: Path) -> None:
    banner("3. replacement policies under sequential flooding (16 frames, 200 pages)")
    rng = random.Random(SEED)
    hot_pages = list(range(10))
    cold_pages = list(range(10, 200))
    accesses = build_workload(rng, hot_pages, cold_pages)
    print(f"access trace           : {len(accesses)} reads, 60% into a 10-page hot set,")
    print("                         40% streaming through 190 cold pages")
    print()
    print(f"{'policy':<10}{'hit rate':>12}{'evictions':>12}{'disk reads':>13}")
    print("-" * 47)
    for policy, k in (("lru", 0), ("clock", 0), ("lru-k", 2), ("lru-k", 3)):
        label = policy.upper() if k == 0 else f"LRU-{k}"
        hit_rate, evictions, reads = run_policy(directory, policy, k or 2, 200, accesses)
        print(f"{label:<10}{hit_rate * 100:>11.1f}%{evictions:>12}{reads:>13}")
    print()
    print("LRU and CLOCK are defeated equally: the scan touches every frame, so the")
    print("hot set is flushed out either by recency order or by a single reference bit.")
    print("LRU-K keeps the hot set resident instead, because a page touched once has an")
    print("infinite backward k-distance and goes ahead of anything with real history.")


def demo_table_heap(directory: Path) -> None:
    banner("4. a slotted-page table heap running on an 8-frame pool")
    disk = DiskManager(directory / "table.db")
    pool = BufferPoolManager(disk, pool_size=8, policy="lru-k", k=2)
    heap = TableHeap(pool)

    rng = random.Random(SEED)
    rids: List[RecordId] = []
    expected: Dict[RecordId, bytes] = {}
    for row_id in range(3000):
        record = struct.pack(">I", row_id) + f"customer-{row_id:05d}-{rng.randrange(10 ** 6):06d}".encode("ascii")
        rid = heap.insert(record)
        rids.append(rid)
        expected[rid] = record

    print(f"rows inserted          : {len(heap)}")
    print(f"pages used             : {heap.page_count} ({disk.num_pages} allocated on disk)")
    print(f"pool frames            : {pool.pool_size}, evictions so far: {pool.stats.evictions}")

    mismatches = sum(1 for rid, record in expected.items() if heap.get(rid) != record)
    print(f"point lookups verified : {len(expected) - mismatches}/{len(expected)} exact")

    pool.stats.reset()
    disk.reset_counters()
    scanned = list(heap.scan())
    print(f"full scan returned     : {len(scanned)} rows, hit rate {pool.stats.hit_rate * 100:.1f}%, disk reads {disk.reads}")

    doomed = rids[::7]
    for rid in doomed:
        heap.delete(rid)
    print(f"rows deleted           : {len(doomed)}, remaining {len(heap)}")
    print(f"deleted row readable   : {heap.try_get(doomed[0]) is not None}")

    victim_page = doomed[0].page_id
    with pool.read_page(victim_page) as page:
        before = SlottedPage(page.data).free_space
    reclaimed = heap.compact_page(victim_page)
    print(f"compaction on page {victim_page:<3} : {before} -> {before + reclaimed} free bytes (+{reclaimed})")
    survivors = sum(1 for rid, record in heap.scan() if expected[rid] == record)
    print(f"survivors intact       : {survivors}/{len(heap)}")

    pool.flush_all_pages()
    disk.close()

    keeper = rids[1]
    reopened_disk = DiskManager(directory / "table.db")
    reopened_pool = BufferPoolManager(reopened_disk, pool_size=4, policy="lru")
    with reopened_pool.read_page(keeper.page_id) as page:
        row = SlottedPage(page.data).get(keeper.slot)
    print(f"after reopening file   : row {struct.unpack('>I', row[:4])[0]} reads back as {row[4:].decode('ascii')}")
    reopened_disk.close()


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="buffer-pool-") as tmp:
        directory = Path(tmp)
        demo_page_lifecycle(directory)
        demo_pinning(directory)
        demo_policies(directory)
        demo_table_heap(directory)
    print()


if __name__ == "__main__":
    main()
