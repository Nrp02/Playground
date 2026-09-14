package di;

@FunctionalInterface
public interface Matcher<T> {
    boolean matches(T candidate);

    default Matcher<T> and(Matcher<? super T> other) {
        return candidate -> matches(candidate) && other.matches(candidate);
    }

    default Matcher<T> or(Matcher<? super T> other) {
        return candidate -> matches(candidate) || other.matches(candidate);
    }

    default Matcher<T> negate() {
        return candidate -> !matches(candidate);
    }
}
