package di;

import java.util.List;

abstract class Binding<T> {
    private final Key<T> key;
    private final Scope scope;
    private final boolean eager;

    Binding(Key<T> key, Scope scope, boolean eager) {
        this.key = key;
        this.scope = scope;
        this.eager = eager;
    }

    final Key<T> key() {
        return key;
    }

    final Scope scope() {
        return scope;
    }

    final boolean eager() {
        return eager;
    }

    boolean managesLifecycle() {
        return true;
    }

    abstract List<Dependency<?>> dependencies();

    abstract T provision(Container container);

    abstract String describe();
}
