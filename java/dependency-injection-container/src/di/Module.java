package di;

@FunctionalInterface
public interface Module {
    void configure(Binder binder);
}
