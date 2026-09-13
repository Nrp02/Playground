import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;
import java.util.SortedSet;
import stream.Aggregator;
import stream.Aggregators;
import stream.Codec;
import stream.Mean;

public final class AggregatorTest {

    private AggregatorTest() {
    }

    public static void run() {
        Assertions.suite("Aggregators");

        Assertions.test("count, sum, min and max fold values", () -> {
            List<Long> values = List.of(4L, 9L, 2L, 7L);
            Assertions.assertEquals("count", 4L, fold(Aggregators.<Long>count(), values));
            Assertions.assertEquals("sum", 22L, fold(Aggregators.<Long>sum(Long::longValue), values));
            Assertions.assertEquals("min", 2L, fold(Aggregators.<Long>min(Long::longValue), values));
            Assertions.assertEquals("max", 9L, fold(Aggregators.<Long>max(Long::longValue), values));
        });

        Assertions.test("average keeps sum and count", () -> {
            Aggregator<Long, Mean, Double> average = Aggregators.average(Long::longValue);
            Assertions.assertEquals("mean", 2.5, fold(average, List.of(1L, 2L, 3L, 4L)));
            Assertions.assertEquals("empty mean", 0.0, average.result(average.createAccumulator()));
        });

        Assertions.test("merge equals folding the concatenation", () -> {
            Aggregator<Long, Mean, Double> average = Aggregators.average(Long::longValue);
            Mean left = accumulate(average, List.of(10L, 20L));
            Mean right = accumulate(average, List.of(30L, 60L, 80L));
            Assertions.assertEquals("merged", accumulate(average, List.of(10L, 20L, 30L, 60L, 80L)),
                average.merge(left, right));
        });

        Assertions.test("distinct count deduplicates and merges as union", () -> {
            Aggregator<String, SortedSet<String>, Integer> distinct = Aggregators.distinctCount(s -> s);
            SortedSet<String> left = accumulate(distinct, List.of("a", "b", "a"));
            SortedSet<String> right = accumulate(distinct, List.of("b", "c"));
            Assertions.assertEquals("left", 2, distinct.result(left));
            Assertions.assertEquals("union", 3, distinct.result(distinct.merge(left, right)));
            Assertions.assertEquals("inputs untouched", 2, left.size());
        });

        Assertions.test("accumulators round trip through their codecs", () -> {
            Aggregator<Long, Mean, Double> average = Aggregators.average(Long::longValue);
            Mean mean = accumulate(average, List.of(3L, 5L, 11L));
            Assertions.assertEquals("mean", mean, roundTrip(average.codec(), mean));
            Aggregator<String, SortedSet<String>, Integer> distinct = Aggregators.distinctCount(s -> s);
            SortedSet<String> set = accumulate(distinct, List.of("x", "y", "ünï"));
            Assertions.assertEquals("set", set, roundTrip(distinct.codec(), set));
            Aggregator<Long, Long, Long> min = Aggregators.min(Long::longValue);
            Assertions.assertEquals("identity", Long.MAX_VALUE, roundTrip(min.codec(), min.createAccumulator()));
        });
    }

    private static <T, A, R> R fold(Aggregator<T, A, R> aggregator, List<T> values) {
        return aggregator.result(accumulate(aggregator, values));
    }

    private static <T, A, R> A accumulate(Aggregator<T, A, R> aggregator, List<T> values) {
        A accumulator = aggregator.createAccumulator();
        for (T value : values) {
            accumulator = aggregator.add(accumulator, value);
        }
        return accumulator;
    }

    private static <V> V roundTrip(Codec<V> codec, V value) {
        try {
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            codec.write(new DataOutputStream(buffer), value);
            return codec.read(new DataInputStream(new ByteArrayInputStream(buffer.toByteArray())));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }
}
