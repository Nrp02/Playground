import random

from skip_list import SkipList


def main() -> int:
    rng = random.Random(42)
    skip_list = SkipList(seed=1)

    keys = list(range(5000))
    rng.shuffle(keys)

    print("=== inserting 5000 keys in random order ===")
    for key in keys:
        skip_list.insert(key, f"value-{key}")
    print(f"size: {len(skip_list)}")

    stats = skip_list.stats()
    print(f"max level reached: {stats['max_level']}")
    print(f"average node height: {stats['average_height']:.3f}")
    print(f"total forward-pointer slots: {stats['total_forward_slots']}")
    print("height distribution (height -> node count):")
    for height in sorted(stats["height_distribution"]):
        print(f"  {height}: {stats['height_distribution'][height]}")

    print("\n=== spot-checking search ===")
    for probe in [0, 1234, 4999, 999999]:
        found, value = skip_list.search(probe)
        print(f"search({probe}) -> found={found} value={value}")

    print("\n=== updating existing keys ===")
    skip_list.insert(1234, "updated-value")
    found, value = skip_list.search(1234)
    print(f"search(1234) after update -> found={found} value={value}")

    print("\n=== range query [100, 110) ===")
    print(list(skip_list.range(100, 110)))

    print("\n=== deleting 2000 random keys ===")
    to_delete = rng.sample(keys, 2000)
    deleted = 0
    for key in to_delete:
        if skip_list.delete(key):
            deleted += 1
    print(f"deleted: {deleted}")
    print(f"size after deletion: {len(skip_list)}")

    missing = to_delete[0]
    found, _ = skip_list.search(missing)
    print(f"search({missing}) after deletion -> found={found}")

    remaining_keys = skip_list.keys()
    print(f"iteration order sorted: {remaining_keys == sorted(remaining_keys)}")

    final_stats = skip_list.stats()
    print(f"\nfinal max level: {final_stats['max_level']}")
    print(f"final average node height: {final_stats['average_height']:.3f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
