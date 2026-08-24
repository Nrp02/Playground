import random
from collections import Counter

from bloom.bloom_filter import BloomFilter
from bloom.count_min_sketch import CountMinSketch


def run_bloom_filter_demo() -> None:
    print("=== bloom filter: empirical false-positive rate ===")
    rng = random.Random(1)
    num_inserted = 20000
    num_probes = 100000
    target_fpr = 0.01

    inserted = [f"member-{i}-{rng.randrange(1 << 30)}" for i in range(num_inserted)]
    probes = [f"absent-{i}-{rng.randrange(1 << 30)}" for i in range(num_probes)]

    bloom = BloomFilter(expected_items=num_inserted, target_fpr=target_fpr)
    for item in inserted:
        bloom.add(item)

    for item in inserted:
        assert bloom.might_contain(item)

    false_positives = sum(1 for item in probes if bloom.might_contain(item))
    empirical_fpr = false_positives / num_probes

    print(f"inserted items: {num_inserted}")
    print(f"bit array size: {bloom.size_bits} bits, hash functions: {bloom.num_hashes}")
    print(f"target false-positive rate: {target_fpr:.4f}")
    print(f"theoretical false-positive rate at this fill: {bloom.estimated_false_positive_rate():.4f}")
    print(f"empirical false-positive rate over {num_probes} probes: {empirical_fpr:.4f}")


def zipfian_stream(vocabulary_size: int, stream_length: int, rng: random.Random) -> list[str]:
    weights = [1.0 / rank for rank in range(1, vocabulary_size + 1)]
    population = [f"word-{i}" for i in range(vocabulary_size)]
    return rng.choices(population, weights=weights, k=stream_length)


def run_count_min_sketch_demo() -> None:
    print("\n=== count-min sketch: frequency estimation on a skewed stream ===")
    rng = random.Random(2)
    vocabulary_size = 2000
    stream_length = 300000

    stream = zipfian_stream(vocabulary_size, stream_length, rng)
    exact_counts = Counter(stream)

    sketch = CountMinSketch(epsilon=0.01, delta=0.1)
    for item in stream:
        sketch.add(item)

    print(f"width: {sketch.width}, depth: {sketch.depth}")
    print(f"stream length: {stream_length}, distinct items: {len(exact_counts)}")

    top_items = exact_counts.most_common(10)
    print(f"{'item':<10}{'exact':>10}{'estimate':>10}{'error':>10}")
    total_error = 0
    for item, exact in top_items:
        estimate = sketch.estimate(item)
        error = estimate - exact
        total_error += abs(error)
        print(f"{item:<10}{exact:>10}{estimate:>10}{error:>10}")

    all_errors = [sketch.estimate(item) - exact for item, exact in exact_counts.items()]
    mean_abs_error = sum(abs(e) for e in all_errors) / len(all_errors)
    max_error = max(all_errors)
    print(f"\nmean absolute error over all {len(exact_counts)} distinct items: {mean_abs_error:.2f}")
    print(f"max overestimate: {max_error}")
    print("count-min sketch never underestimates: "
          f"{all(e >= 0 for e in all_errors)}")


def main() -> int:
    run_bloom_filter_demo()
    run_count_min_sketch_demo()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
