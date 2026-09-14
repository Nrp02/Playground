package di;

import java.util.List;

final class InstanceBinding<T> extends Binding<T> {
    private final T instance;

    InstanceBinding(Key<T> key, T instance, boolean eager) {
        super(key, Scope.SINGLETON, eager);
        this.instance = instance;
    }

    @Override
    boolean managesLifecycle() {
        return false;
    }

    T instance() {
        return instance;
    }

    @Override
    List<Dependency<?>> dependencies() {
        return List.of();
    }

    @Override
    T provision(Container container) {
        return instance;
    }

    @Override
    String describe() {
        return "instance of " + Reflection.name(instance.getClass());
    }
}
