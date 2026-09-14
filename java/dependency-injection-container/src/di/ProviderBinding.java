package di;

import java.util.List;

final class ProviderBinding<T> extends Binding<T> {
    private final Provider<? extends T> provider;

    ProviderBinding(Key<T> key, Provider<? extends T> provider, Scope scope, boolean eager) {
        super(key, scope, eager);
        this.provider = provider;
    }

    @Override
    List<Dependency<?>> dependencies() {
        return List.of();
    }

    @Override
    T provision(Container container) {
        T value;
        try {
            value = provider.get();
        } catch (RuntimeException e) {
            throw Reflection.propagate("Provider for " + key(), e);
        }
        if (value == null) {
            throw new ProvisionException("Provider for " + key() + " returned null");
        }
        return value;
    }

    @Override
    String describe() {
        return "provider";
    }
}
