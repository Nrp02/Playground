package stream;

public interface Aggregator<T, A, R> {

    A createAccumulator();

    A add(A accumulator, T value);

    A merge(A left, A right);

    R result(A accumulator);

    Codec<A> codec();
}
