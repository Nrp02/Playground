package di;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Objects;
import java.util.Set;

public final class Binder {
    private final List<BindingBuilder<?>> builders = new ArrayList<>();
    private final List<Binding<?>> providesBindings = new ArrayList<>();
    private final List<InterceptorBinding> interceptors = new ArrayList<>();
    private final List<String> errors = new ArrayList<>();
    private final Set<Module> installed = Collections.newSetFromMap(new IdentityHashMap<>());

    Binder() {
    }

    public <T> BindingBuilder<T> bind(Class<T> type) {
        return bind(Key.of(type));
    }

    public <T> BindingBuilder<T> bind(Key<T> key) {
        BindingBuilder<T> builder = new BindingBuilder<>(Objects.requireNonNull(key, "key"));
        builders.add(builder);
        return builder;
    }

    public void bindInterceptor(Matcher<? super Class<?>> classes, Matcher<? super Method> methods,
                                MethodInterceptor... chain) {
        if (chain.length == 0) {
            throw new IllegalArgumentException("at least one interceptor is required");
        }
        interceptors.add(new InterceptorBinding(Objects.requireNonNull(classes), Objects.requireNonNull(methods),
            List.of(chain)));
    }

    public void install(Module module) {
        Objects.requireNonNull(module, "module");
        if (!installed.add(module)) {
            return;
        }
        module.configure(this);
        for (Class<?> current = module.getClass(); current != null && current != Object.class;
             current = current.getSuperclass()) {
            Method[] declared = current.getDeclaredMethods();
            Arrays.sort(declared, Comparator.comparing(Method::getName));
            for (Method method : declared) {
                if (!method.isAnnotationPresent(Provides.class)) {
                    continue;
                }
                try {
                    providesBindings.add(ProvidesMethodBinding.create(module, method));
                } catch (ConfigurationException e) {
                    errors.addAll(e.messages());
                }
            }
        }
    }

    public void addError(String message) {
        errors.add(Objects.requireNonNull(message, "message"));
    }

    List<Binding<?>> bindings(List<String> sink) {
        sink.addAll(errors);
        List<Binding<?>> result = new ArrayList<>();
        for (BindingBuilder<?> builder : builders) {
            try {
                result.add(builder.build());
            } catch (ConfigurationException e) {
                sink.addAll(e.messages());
            }
        }
        result.addAll(providesBindings);
        return result;
    }

    List<InterceptorBinding> interceptors() {
        return List.copyOf(interceptors);
    }
}
