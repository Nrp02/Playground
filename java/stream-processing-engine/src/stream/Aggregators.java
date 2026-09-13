package stream;

import java.util.Collections;
import java.util.SortedSet;
import java.util.TreeSet;
import java.util.function.BiFunction;
import java.util.function.BinaryOperator;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.function.ToLongFunction;

public final class Aggregators {

    private Aggregators() {
    }

    public static <T, A, R> Aggregator<T, A, R> of(Supplier<A> create, BiFunction<A, T, A> add,
                                                   BinaryOperator<A> merge, Function<A, R> result,
                                                   Codec<A> codec) {
        return new Composed<>(create, add, merge, result, codec);
    }

    public static <T> Aggregator<T, Long, Long> count() {
        return of(() -> 0L, (acc, value) -> acc + 1, Long::sum, Function.identity(), Codecs.LONG);
    }

    public static <T> Aggregator<T, Long, Long> sum(ToLongFunction<T> field) {
        return of(() -> 0L, (acc, value) -> acc + field.applyAsLong(value), Long::sum,
            Function.identity(), Codecs.LONG);
    }

    public static <T> Aggregator<T, Long, Long> max(ToLongFunction<T> field) {
        return of(() -> Long.MIN_VALUE, (acc, value) -> Math.max(acc, field.applyAsLong(value)),
            Math::max, Function.identity(), Codecs.LONG);
    }

    public static <T> Aggregator<T, Long, Long> min(ToLongFunction<T> field) {
        return of(() -> Long.MAX_VALUE, (acc, value) -> Math.min(acc, field.applyAsLong(value)),
            Math::min, Function.identity(), Codecs.LONG);
    }

    public static <T> Aggregator<T, Mean, Double> average(ToLongFunction<T> field) {
        return of(() -> Mean.EMPTY, (acc, value) -> acc.plus(field.applyAsLong(value)),
            Mean::combine, Mean::value, Codecs.MEAN);
    }

    public static <T> Aggregator<T, SortedSet<String>, Integer> distinctCount(Function<T, String> field) {
        return of(Collections::emptySortedSet,
            (acc, value) -> withElement(acc, field.apply(value)),
            Aggregators::union,
            SortedSet::size,
            Codecs.STRING_SET);
    }

    private static SortedSet<String> withElement(SortedSet<String> set, String element) {
        if (set.contains(element)) {
            return set;
        }
        TreeSet<String> copy = new TreeSet<>(set);
        copy.add(element);
        return Collections.unmodifiableSortedSet(copy);
    }

    private static SortedSet<String> union(SortedSet<String> left, SortedSet<String> right) {
        TreeSet<String> copy = new TreeSet<>(left);
        copy.addAll(right);
        return Collections.unmodifiableSortedSet(copy);
    }

    private record Composed<T, A, R>(Supplier<A> create, BiFunction<A, T, A> adder,
                                     BinaryOperator<A> merger, Function<A, R> finisher,
                                     Codec<A> stateCodec) implements Aggregator<T, A, R> {

        @Override
        public A createAccumulator() {
            return create.get();
        }

        @Override
        public A add(A accumulator, T value) {
            return adder.apply(accumulator, value);
        }

        @Override
        public A merge(A left, A right) {
            return merger.apply(left, right);
        }

        @Override
        public R result(A accumulator) {
            return finisher.apply(accumulator);
        }

        @Override
        public Codec<A> codec() {
            return stateCodec;
        }
    }
}
