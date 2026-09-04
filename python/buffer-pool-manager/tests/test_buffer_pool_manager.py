import random
import struct
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from bufferpool import (
    BufferPoolManager,
    ClockReplacer,
    DiskManager,
    InvalidPageError,
    LRUKReplacer,
    LRUReplacer,
    PageFullError,
    PageNotResidentError,
    PageStillPinnedError,
    PoolExhaustedError,
    RecordNotFoundError,
    SlottedPage,
    TableHeap,
    make_replacer,
)


class TempDirCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="bpm-test-")
        self.directory = Path(self._tmp.name)
        self._disks = []

    def tearDown(self):
        for disk in self._disks:
            disk.close()
        self._tmp.cleanup()

    def make_disk(self, name="test.db", page_size=4096):
        disk = DiskManager(self.directory / name, page_size=page_size)
        self._disks.append(disk)
        return disk

    def make_pool(self, pool_size=4, policy="lru", k=2, name="test.db", page_size=4096):
        disk = self.make_disk(name=name, page_size=page_size)
        return disk, BufferPoolManager(disk, pool_size=pool_size, policy=policy, k=k)


class TestDiskManager(TempDirCase):
    def test_allocate_returns_sequential_ids_and_zeroed_pages(self):
        disk = self.make_disk()
        ids = [disk.allocate_page() for _ in range(4)]
        self.assertEqual(ids, [0, 1, 2, 3])
        self.assertEqual(disk.num_pages, 4)
        self.assertEqual(disk.read_page(2), bytearray(disk.page_size))

    def test_write_then_read_round_trips(self):
        disk = self.make_disk()
        page_id = disk.allocate_page()
        disk.write_page(page_id, b"hello disk")
        data = disk.read_page(page_id)
        self.assertEqual(bytes(data[:10]), b"hello disk")
        self.assertEqual(len(data), disk.page_size)

    def test_short_write_is_zero_padded(self):
        disk = self.make_disk()
        page_id = disk.allocate_page()
        disk.write_page(page_id, b"x" * 8)
        disk.write_page(page_id, b"y")
        data = disk.read_page(page_id)
        self.assertEqual(bytes(data[:8]), b"y" + bytes(7))

    def test_oversized_write_rejected(self):
        disk = self.make_disk(page_size=64)
        page_id = disk.allocate_page()
        with self.assertRaises(ValueError):
            disk.write_page(page_id, b"z" * 65)

    def test_invalid_page_access_raises(self):
        disk = self.make_disk()
        disk.allocate_page()
        with self.assertRaises(InvalidPageError):
            disk.read_page(7)
        with self.assertRaises(InvalidPageError):
            disk.read_page(-1)

    def test_deallocated_page_id_is_reused_and_zeroed(self):
        disk = self.make_disk()
        first = disk.allocate_page()
        second = disk.allocate_page()
        disk.write_page(first, b"stale bytes")
        disk.deallocate_page(first)
        recycled = disk.allocate_page()
        self.assertEqual(recycled, first)
        self.assertNotEqual(recycled, second)
        self.assertEqual(disk.read_page(recycled), bytearray(disk.page_size))
        self.assertEqual(disk.num_pages, 2)

    def test_counters_track_io(self):
        disk = self.make_disk()
        page_id = disk.allocate_page()
        disk.reset_counters()
        disk.read_page(page_id)
        disk.read_page(page_id)
        disk.write_page(page_id, b"a")
        self.assertEqual(disk.reads, 2)
        self.assertEqual(disk.writes, 1)

    def test_contents_persist_across_reopen(self):
        path = self.directory / "persist.db"
        disk = DiskManager(path)
        page_id = disk.allocate_page()
        disk.write_page(page_id, b"durable")
        disk.close()

        reopened = DiskManager(path)
        self._disks.append(reopened)
        self.assertEqual(reopened.num_pages, 1)
        self.assertEqual(bytes(reopened.read_page(page_id)[:7]), b"durable")

    def test_file_that_is_not_a_whole_number_of_pages_is_rejected(self):
        path = self.directory / "ragged.db"
        path.write_bytes(b"x" * 10)
        with self.assertRaises(ValueError):
            DiskManager(path, page_size=64)


class TestLRUReplacer(unittest.TestCase):
    def test_evicts_least_recently_accessed(self):
        replacer = LRUReplacer()
        for frame in (0, 1, 2):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        self.assertEqual(replacer.evict(), 0)
        self.assertEqual(replacer.evict(), 1)
        self.assertEqual(replacer.evict(), 2)
        self.assertIsNone(replacer.evict())

    def test_reaccess_moves_frame_to_the_back(self):
        replacer = LRUReplacer()
        for frame in (0, 1, 2):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.record_access(0)
        self.assertEqual(replacer.evict(), 1)

    def test_non_evictable_frames_are_skipped(self):
        replacer = LRUReplacer()
        for frame in (0, 1, 2):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.set_evictable(0, False)
        self.assertEqual(replacer.evictable_size(), 2)
        self.assertEqual(replacer.evict(), 1)

    def test_remove_drops_frame_entirely(self):
        replacer = LRUReplacer()
        replacer.record_access(5)
        replacer.set_evictable(5, True)
        replacer.remove(5)
        self.assertEqual(replacer.tracked_size(), 0)
        self.assertIsNone(replacer.evict())

    def test_untracked_frame_cannot_be_made_evictable(self):
        replacer = LRUReplacer()
        replacer.set_evictable(9, True)
        self.assertEqual(replacer.evictable_size(), 0)


class TestClockReplacer(unittest.TestCase):
    def test_second_chance_spares_a_reaccessed_frame_then_takes_it(self):
        replacer = ClockReplacer()
        for frame in (0, 1, 2):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.record_access(0)
        self.assertEqual(replacer.evict(), 0)

    def test_clock_differs_from_lru_on_the_same_trace(self):
        trace = [0, 1, 2, 0]
        clock = ClockReplacer()
        lru = LRUReplacer()
        for replacer in (clock, lru):
            for frame in trace:
                replacer.record_access(frame)
                replacer.set_evictable(frame, True)
        self.assertEqual(clock.evict(), 0)
        self.assertEqual(lru.evict(), 1)

    def test_pinned_frame_is_never_chosen(self):
        replacer = ClockReplacer()
        for frame in (0, 1, 2):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.set_evictable(1, False)
        victims = [replacer.evict(), replacer.evict()]
        self.assertNotIn(1, victims)
        self.assertIsNone(replacer.evict())

    def test_remove_keeps_the_hand_consistent(self):
        replacer = ClockReplacer()
        for frame in range(4):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.remove(2)
        self.assertEqual(replacer.tracked_size(), 3)
        seen = set()
        for _ in range(3):
            seen.add(replacer.evict())
        self.assertEqual(seen, {0, 1, 3})

    def test_no_evictable_frames_returns_none(self):
        replacer = ClockReplacer()
        replacer.record_access(0)
        self.assertIsNone(replacer.evict())


class TestLRUKReplacer(unittest.TestCase):
    def test_frames_with_fewer_than_k_accesses_go_first(self):
        replacer = LRUKReplacer(k=2)
        for frame in (0, 1, 2):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.record_access(0)
        replacer.record_access(1)
        self.assertEqual(replacer.evict(), 2)

    def test_ties_among_infinite_distances_break_by_earliest_access(self):
        replacer = LRUKReplacer(k=2)
        for frame in (7, 8, 9):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        self.assertEqual(replacer.evict(), 7)
        self.assertEqual(replacer.evict(), 8)

    def test_largest_backward_k_distance_wins(self):
        replacer = LRUKReplacer(k=2)
        for frame in (0, 1):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        replacer.record_access(0)
        replacer.record_access(1)
        replacer.record_access(1)
        self.assertEqual(replacer.evict(), 0)

    def test_k_of_one_behaves_like_plain_lru(self):
        lru_k = LRUKReplacer(k=1)
        lru = LRUReplacer()
        for replacer in (lru_k, lru):
            for frame in (0, 1, 2, 0):
                replacer.record_access(frame)
                replacer.set_evictable(frame, True)
        self.assertEqual(lru_k.evict(), lru.evict())

    def test_hot_frames_survive_a_scan(self):
        replacer = LRUKReplacer(k=2)
        for frame in (0, 1):
            for _ in range(3):
                replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        for frame in (2, 3, 4):
            replacer.record_access(frame)
            replacer.set_evictable(frame, True)
        victims = {replacer.evict() for _ in range(3)}
        self.assertEqual(victims, {2, 3, 4})

    def test_invalid_k_rejected(self):
        with self.assertRaises(ValueError):
            LRUKReplacer(k=0)

    def test_make_replacer_names_and_errors(self):
        self.assertEqual(make_replacer("lru").name, "LRU")
        self.assertEqual(make_replacer("clock").name, "CLOCK")
        self.assertEqual(make_replacer("lru-k", 3).name, "LRU-3")
        with self.assertRaises(ValueError):
            make_replacer("random")


class TestBufferPoolManager(TempDirCase):
    def test_new_page_is_pinned_and_registered(self):
        _, pool = self.make_pool(pool_size=3)
        page = pool.new_page()
        self.assertEqual(pool.pin_count(page.page_id), 1)
        self.assertEqual(pool.resident_page_ids(), [page.page_id])
        self.assertEqual(pool.free_frames(), 2)

    def test_pool_size_must_be_positive(self):
        disk = self.make_disk()
        with self.assertRaises(ValueError):
            BufferPoolManager(disk, pool_size=0)

    def test_data_survives_eviction_and_refetch(self):
        _, pool = self.make_pool(pool_size=2)
        ids = []
        for index in range(6):
            with pool.create_page() as page:
                page.write(f"row-{index}".encode("ascii"))
                ids.append(page.page_id)
        for index, page_id in enumerate(ids):
            with pool.read_page(page_id) as page:
                self.assertEqual(page.read(0, 8).rstrip(b"\x00"), f"row-{index}".encode("ascii"))

    def test_dirty_page_is_written_back_exactly_once_on_eviction(self):
        disk, pool = self.make_pool(pool_size=1)
        first = pool.new_page()
        first.write(b"dirty")
        pool.unpin_page(first.page_id, is_dirty=True)
        disk.reset_counters()

        second = pool.new_page()
        pool.unpin_page(second.page_id, is_dirty=False)
        self.assertEqual(pool.stats.dirty_writebacks, 1)
        self.assertEqual(bytes(disk.read_page(first.page_id)[:5]), b"dirty")

    def test_clean_page_eviction_writes_nothing(self):
        disk, pool = self.make_pool(pool_size=1)
        page_id = disk.allocate_page()
        other_id = disk.allocate_page()
        pool.unpin_page(pool.fetch_page(page_id).page_id, is_dirty=False)
        disk.reset_counters()
        pool.unpin_page(pool.fetch_page(other_id).page_id, is_dirty=False)
        self.assertEqual(pool.stats.clean_evictions, 1)
        self.assertEqual(pool.stats.dirty_writebacks, 0)
        self.assertEqual(disk.writes, 0)

    def test_all_pinned_pool_refuses_to_evict(self):
        disk, pool = self.make_pool(pool_size=2)
        spare = disk.allocate_page()
        held = [pool.new_page(), pool.new_page()]
        with self.assertRaises(PoolExhaustedError):
            pool.fetch_page(spare)
        with self.assertRaises(PoolExhaustedError):
            pool.new_page()
        held[0].unpin()
        refetched = pool.fetch_page(spare)
        self.assertEqual(refetched.page_id, spare)

    def test_pin_count_tracks_repeated_fetches(self):
        _, pool = self.make_pool(pool_size=2)
        page = pool.new_page()
        pool.fetch_page(page.page_id)
        pool.fetch_page(page.page_id)
        self.assertEqual(pool.pin_count(page.page_id), 3)
        self.assertTrue(pool.unpin_page(page.page_id))
        self.assertEqual(pool.pin_count(page.page_id), 2)

    def test_unpin_of_unknown_or_unpinned_page_returns_false(self):
        _, pool = self.make_pool(pool_size=2)
        self.assertFalse(pool.unpin_page(42))
        page = pool.new_page()
        self.assertTrue(pool.unpin_page(page.page_id))
        self.assertFalse(pool.unpin_page(page.page_id))

    def test_pin_count_of_non_resident_page_raises(self):
        disk, pool = self.make_pool(pool_size=1)
        page_id = disk.allocate_page()
        with self.assertRaises(PageNotResidentError):
            pool.pin_count(page_id)
        with self.assertRaises(PageNotResidentError):
            pool.is_dirty(page_id)

    def test_fetching_a_page_that_does_not_exist_raises(self):
        _, pool = self.make_pool(pool_size=2)
        with self.assertRaises(InvalidPageError):
            pool.fetch_page(99)

    def test_flush_page_clears_dirty_flag_and_hits_disk(self):
        disk, pool = self.make_pool(pool_size=2)
        page = pool.new_page()
        page.write(b"flushed")
        self.assertTrue(pool.is_dirty(page.page_id))
        self.assertTrue(pool.flush_page(page.page_id))
        self.assertFalse(pool.is_dirty(page.page_id))
        self.assertEqual(bytes(disk.read_page(page.page_id)[:7]), b"flushed")
        self.assertFalse(pool.flush_page(1234))

    def test_flush_all_pages_writes_every_resident_page(self):
        disk, pool = self.make_pool(pool_size=4)
        pages = [pool.new_page() for _ in range(3)]
        for index, page in enumerate(pages):
            page.write(f"p{index}".encode("ascii"))
        self.assertEqual(pool.flush_all_pages(), 3)
        for index, page in enumerate(pages):
            self.assertEqual(bytes(disk.read_page(page.page_id)[:2]), f"p{index}".encode("ascii"))
            self.assertFalse(pool.is_dirty(page.page_id))

    def test_delete_page_frees_the_frame_and_the_page_id(self):
        disk, pool = self.make_pool(pool_size=2)
        page = pool.new_page()
        page_id = page.page_id
        pool.unpin_page(page_id)
        self.assertTrue(pool.delete_page(page_id))
        self.assertEqual(pool.resident_page_ids(), [])
        self.assertEqual(pool.free_frames(), 2)
        self.assertEqual(disk.allocate_page(), page_id)

    def test_delete_page_of_pinned_page_raises(self):
        _, pool = self.make_pool(pool_size=2)
        page = pool.new_page()
        with self.assertRaises(PageStillPinnedError):
            pool.delete_page(page.page_id)

    def test_stats_count_hits_and_misses(self):
        disk, pool = self.make_pool(pool_size=2)
        page_id = disk.allocate_page()
        pool.unpin_page(pool.fetch_page(page_id).page_id)
        pool.unpin_page(pool.fetch_page(page_id).page_id)
        pool.unpin_page(pool.fetch_page(page_id).page_id)
        self.assertEqual(pool.stats.misses, 1)
        self.assertEqual(pool.stats.hits, 2)
        self.assertAlmostEqual(pool.stats.hit_rate, 2 / 3)
        self.assertEqual(pool.stats.lookups, 3)
        pool.stats.reset()
        self.assertEqual(pool.stats.lookups, 0)
        self.assertEqual(pool.stats.hit_rate, 0.0)

    def test_write_guard_unpins_and_marks_dirty(self):
        disk, pool = self.make_pool(pool_size=2)
        page_id = disk.allocate_page()
        with pool.write_page(page_id) as page:
            page.write(b"guarded")
            self.assertEqual(pool.pin_count(page_id), 1)
        self.assertEqual(pool.pin_count(page_id), 0)
        self.assertTrue(pool.is_dirty(page_id))

    def test_read_guard_leaves_page_clean(self):
        disk, pool = self.make_pool(pool_size=2)
        page_id = disk.allocate_page()
        with pool.read_page(page_id) as page:
            page.read(0, 4)
        self.assertEqual(pool.pin_count(page_id), 0)
        self.assertFalse(pool.is_dirty(page_id))

    def test_guard_release_is_idempotent(self):
        _, pool = self.make_pool(pool_size=2)
        page = pool.new_page()
        pool.unpin_page(page.page_id)
        guard = pool.read_page(page.page_id)
        guard.release()
        guard.release()
        self.assertEqual(pool.pin_count(page.page_id), 0)

    def test_page_write_past_the_end_is_rejected(self):
        _, pool = self.make_pool(pool_size=2, page_size=64)
        page = pool.new_page()
        with self.assertRaises(ValueError):
            page.write(b"x" * 65)
        with self.assertRaises(ValueError):
            page.write(b"x" * 8, offset=60)

    def test_lru_k_pool_outperforms_lru_under_sequential_flooding(self):
        rng = random.Random(7)
        accesses = []
        cursor = 0
        cold = list(range(8, 120))
        for _ in range(30):
            accesses.extend(rng.randrange(8) for _ in range(20))
            for _ in range(12):
                accesses.append(cold[cursor % len(cold)])
                cursor += 1

        rates = {}
        for policy, name in (("lru", "lru"), ("lru-k", "lru-k")):
            disk = self.make_disk(name=f"flood-{name}.db")
            for _ in range(120):
                disk.allocate_page()
            pool = BufferPoolManager(disk, pool_size=12, policy=policy, k=2)
            for page_id in accesses:
                pool.unpin_page(pool.fetch_page(page_id).page_id)
            rates[name] = pool.stats.hit_rate
        self.assertGreater(rates["lru-k"], rates["lru"] + 0.05)

    def test_random_operations_match_a_reference_model(self):
        disk, pool = self.make_pool(pool_size=4, policy="clock", page_size=256)
        rng = random.Random(99)
        model = {}
        for _ in range(400):
            choice = rng.random()
            if not model or choice < 0.2:
                with pool.create_page() as page:
                    payload = bytes(rng.randrange(256) for _ in range(16))
                    page.write(payload)
                    model[page.page_id] = payload
            elif choice < 0.7:
                page_id = rng.choice(sorted(model))
                with pool.read_page(page_id) as page:
                    self.assertEqual(page.read(0, 16), model[page_id])
            elif choice < 0.9:
                page_id = rng.choice(sorted(model))
                payload = bytes(rng.randrange(256) for _ in range(16))
                with pool.write_page(page_id) as page:
                    page.write(payload)
                model[page_id] = payload
            else:
                page_id = rng.choice(sorted(model))
                pool.delete_page(page_id)
                del model[page_id]

        for page_id, payload in model.items():
            with pool.read_page(page_id) as page:
                self.assertEqual(page.read(0, 16), payload)

    def test_concurrent_access_keeps_pages_consistent(self):
        disk, pool = self.make_pool(pool_size=6, policy="lru-k")
        page_ids = [disk.allocate_page() for _ in range(24)]
        errors = []

        def worker(worker_id):
            try:
                mine = page_ids[worker_id::6]
                for round_index in range(40):
                    for page_id in mine:
                        payload = struct.pack(">III", worker_id, page_id, round_index)
                        with pool.write_page(page_id) as page:
                            page.write(payload)
                        with pool.read_page(page_id) as page:
                            self.assertEqual(page.read(0, 12), payload)
            except Exception as exc:
                errors.append(exc)

        threads = [threading.Thread(target=worker, args=(index,)) for index in range(6)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

        self.assertEqual(errors, [])
        self.assertEqual(pool.pinned_page_ids(), [])
        pool.flush_all_pages()
        for worker_id in range(6):
            for page_id in page_ids[worker_id::6]:
                expected = struct.pack(">III", worker_id, page_id, 39)
                self.assertEqual(bytes(disk.read_page(page_id)[:12]), expected)


class TestSlottedPage(unittest.TestCase):
    def make_page(self, size=128):
        return SlottedPage(bytearray(size), initialize=True)

    def test_insert_and_read_back(self):
        page = self.make_page()
        first = page.insert(b"alpha")
        second = page.insert(b"beta")
        self.assertEqual(page.get(first), b"alpha")
        self.assertEqual(page.get(second), b"beta")
        self.assertEqual(page.slot_count, 2)
        self.assertEqual(page.record_count(), 2)

    def test_free_space_accounting(self):
        page = self.make_page(size=64)
        before = page.free_space
        page.insert(b"1234567890")
        self.assertEqual(page.free_space, before - 10 - 4)

    def test_empty_record_rejected(self):
        page = self.make_page()
        with self.assertRaises(ValueError):
            page.insert(b"")

    def test_page_full_raises(self):
        page = self.make_page(size=32)
        page.insert(b"x" * 20)
        self.assertFalse(page.can_fit(b"y" * 20))
        with self.assertRaises(PageFullError):
            page.insert(b"y" * 20)

    def test_delete_creates_a_tombstone_and_hides_the_record(self):
        page = self.make_page()
        slot = page.insert(b"gone")
        keeper = page.insert(b"stays")
        self.assertTrue(page.delete(slot))
        self.assertFalse(page.delete(slot))
        self.assertIsNone(page.try_get(slot))
        with self.assertRaises(RecordNotFoundError):
            page.get(slot)
        self.assertEqual(page.get(keeper), b"stays")
        self.assertEqual([entry[0] for entry in page.iter_records()], [keeper])

    def test_tombstoned_slot_is_reused(self):
        page = self.make_page()
        slot = page.insert(b"first")
        page.delete(slot)
        reused = page.insert(b"second")
        self.assertEqual(reused, slot)
        self.assertEqual(page.slot_count, 1)
        self.assertEqual(page.get(slot), b"second")

    def test_out_of_range_slot_raises(self):
        page = self.make_page()
        with self.assertRaises(RecordNotFoundError):
            page.get(3)
        self.assertIsNone(page.try_get(3))
        self.assertFalse(page.delete(-1))

    def test_compaction_reclaims_dead_space_without_moving_slots(self):
        page = self.make_page(size=256)
        slots = [page.insert(f"record-{index:03d}".encode("ascii")) for index in range(8)]
        for slot in slots[::2]:
            page.delete(slot)
        before = page.free_space
        reclaimed = page.compact()
        self.assertGreater(reclaimed, 0)
        self.assertEqual(page.free_space, before + reclaimed)
        for index, slot in enumerate(slots):
            if index % 2 == 0:
                self.assertIsNone(page.try_get(slot))
            else:
                self.assertEqual(page.get(slot), f"record-{index:03d}".encode("ascii"))

    def test_records_are_stable_under_random_operations(self):
        page = self.make_page(size=1024)
        rng = random.Random(3)
        live = {}
        for _ in range(300):
            if live and rng.random() < 0.4:
                slot = rng.choice(sorted(live))
                page.delete(slot)
                del live[slot]
            else:
                payload = bytes(rng.randrange(97, 123) for _ in range(rng.randrange(1, 12)))
                if not page.can_fit(payload):
                    page.compact()
                    if not page.can_fit(payload):
                        continue
                live[page.insert(payload)] = payload
            for slot, payload in live.items():
                self.assertEqual(page.get(slot), payload)


class TestTableHeap(TempDirCase):
    def test_inserts_spill_across_pages_and_scan_returns_everything(self):
        disk, pool = self.make_pool(pool_size=3, policy="lru-k", page_size=512)
        heap = TableHeap(pool)
        expected = {}
        for index in range(200):
            record = f"row-{index:04d}-{'x' * (index % 17)}".encode("ascii")
            expected[heap.insert(record)] = record
        self.assertEqual(len(heap), 200)
        self.assertGreater(heap.page_count, 1)
        self.assertEqual(dict(heap.scan()), expected)
        self.assertEqual(pool.pinned_page_ids(), [])

    def test_point_lookups_after_eviction(self):
        disk, pool = self.make_pool(pool_size=2, page_size=256)
        heap = TableHeap(pool)
        rids = [heap.insert(f"value-{index}".encode("ascii")) for index in range(100)]
        self.assertGreater(pool.stats.evictions, 0)
        for index, rid in enumerate(rids):
            self.assertEqual(heap.get(rid), f"value-{index}".encode("ascii"))

    def test_delete_removes_the_row_only_once(self):
        _, pool = self.make_pool(pool_size=3, page_size=256)
        heap = TableHeap(pool)
        rid = heap.insert(b"doomed")
        keeper = heap.insert(b"keeper")
        self.assertTrue(heap.delete(rid))
        self.assertFalse(heap.delete(rid))
        self.assertEqual(len(heap), 1)
        self.assertIsNone(heap.try_get(rid))
        with self.assertRaises(RecordNotFoundError):
            heap.get(rid)
        self.assertEqual(heap.get(keeper), b"keeper")

    def test_unknown_page_is_not_a_row(self):
        _, pool = self.make_pool(pool_size=2)
        heap = TableHeap(pool)
        from bufferpool import RecordId

        self.assertIsNone(heap.try_get(RecordId(77, 0)))
        self.assertFalse(heap.delete(RecordId(77, 0)))

    def test_record_larger_than_a_page_is_rejected(self):
        _, pool = self.make_pool(pool_size=2, page_size=128)
        heap = TableHeap(pool)
        with self.assertRaises(PageFullError):
            heap.insert(b"x" * 200)

    def test_freed_space_is_reused_after_compaction(self):
        _, pool = self.make_pool(pool_size=3, page_size=256)
        heap = TableHeap(pool)
        rids = [heap.insert(f"record-{index:03d}".encode("ascii")) for index in range(10)]
        target_page = rids[0].page_id
        for rid in rids:
            if rid.page_id == target_page:
                heap.delete(rid)
        reclaimed = heap.compact_page(target_page)
        self.assertGreater(reclaimed, 0)
        with pool.read_page(target_page) as page:
            self.assertEqual(SlottedPage(page.data).record_count(), 0)

    def test_rows_survive_flush_and_reopen(self):
        path_name = "heap-durable.db"
        disk, pool = self.make_pool(pool_size=2, page_size=256, name=path_name)
        heap = TableHeap(pool)
        rids = [heap.insert(f"durable-{index}".encode("ascii")) for index in range(60)]
        pool.flush_all_pages()
        disk.close()

        reopened = DiskManager(self.directory / path_name, page_size=256)
        self._disks.append(reopened)
        fresh_pool = BufferPoolManager(reopened, pool_size=2, policy="lru")
        for index, rid in enumerate(rids):
            with fresh_pool.read_page(rid.page_id) as page:
                self.assertEqual(SlottedPage(page.data).get(rid.slot), f"durable-{index}".encode("ascii"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
