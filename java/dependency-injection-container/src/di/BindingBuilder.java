package di;

import java.lang.annotation.Annotation;
import java.util.Objects;

public final class BindingBuilder<T> {
    private Key<T> key;
    private Class<? extends T> implementation;
    private T instance;
    private Provider<? extends T> provider;
    private Scope scope;
    private boolean eager;
    private int targets;

    BindingBuilder(Key<T> key) {
        this.key = key;
    }

    public BindingBuilder<T> annotatedWith(Annotation qualifier) {
        key = Key.of(key.type(), Objects.requireNonNull(qualifier, "qualifier"));
        return this;
    }

    public BindingBuilder<T> named(String name) {
        return annotatedWith(Names.named(name));
    }

    public BindingBuilder<T> to(Class<? extends T> implementation) {
        this.implementation = Objects.requireNonNull(implementation, "implementation");
        targets++;
        return this;
    }

    public BindingBuilder<T> toInstance(T instance) {
        this.instance = Objects.requireNonNull(instance, "instance");
        targets++;
        return this;
    }

    public BindingBuilder<T> toProvider(Provider<? extends T> provider) {
        this.provider = Objects.requireNonNull(provider, "provider");
        targets++;
        return this;
    }

    public BindingBuilder<T> in(Scope scope) {
        this.scope = Objects.requireNonNull(scope, "scope");
        return this;
    }

    public BindingBuilder<T> asEagerSingleton() {
        this.scope = Scope.SINGLETON;
        this.eager = true;
        return this;
    }

    Binding<T> build() {
        if (targets > 1) {
            throw new ConfigurationException(key + " is bound to more than one target");
        }
        if (instance != null) {
            if (scope == Scope.PROTOTYPE) {
                throw new ConfigurationException(key + " is bound to an instance and cannot be PROTOTYPE");
            }
            return new InstanceBinding<>(key, instance, eager);
        }
        if (provider != null) {
            return new ProviderBinding<>(key, provider, scope != null ? scope : Scope.PROTOTYPE, eager);
        }
        if (implementation != null && !implementation.equals(key.type())) {
            return new LinkedBinding<>(key, Key.of(implementation), scope != null ? scope : Scope.PROTOTYPE, eager);
        }
        return ConstructorBinding.create(key, key.type(), scope, eager);
    }
}
