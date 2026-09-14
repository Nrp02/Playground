package di;

@FunctionalInterface
public interface Provider<T> {
    T get();
}
