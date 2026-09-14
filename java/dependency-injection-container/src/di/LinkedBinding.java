package di;

import java.util.List;

final class LinkedBinding<T> extends Binding<T> {
    private final Key<? extends T> target;

    LinkedBinding(Key<T> key, Key<? extends T> target, Scope scope, boolean eager) {
        super(key, scope, eager);
        this.target = target;
    }

    @Override
    List<Dependency<?>> dependencies() {
        return List.of(new Dependency<>(target, false, "binding target of " + key()));
    }

    @Override
    T provision(Container container) {
        return container.getInstance(target);
    }

    @Override
    String describe() {
        return "linked to " + target;
    }
}
