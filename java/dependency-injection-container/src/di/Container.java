package di;

import java.lang.reflect.Method;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public final class Container implements AutoCloseable {
    private static final ThreadLocal<ArrayDeque<Key<?>>> RESOLVING = ThreadLocal.withInitial(ArrayDeque::new);
    private static final Key<Container> SELF = Key.of(Container.class);
    private static final Object[] NO_ARGUMENTS = new Object[0];

    private final Container parent;
    private final Map<Key<?>, Binding<?>> explicit;
    private final ConcurrentHashMap<Key<?>, Binding<?>> justInTime = new ConcurrentHashMap<>();
    private final List<InterceptorBinding> interceptors;
    private final Object lock = new Object();
    private final Map<Key<?>, Object> singletons = new HashMap<>();
    private final List<Object> destroyOrder = new ArrayList<>();
    private final Set<Object> tracked = Collections.newSetFromMap(new IdentityHashMap<>());
    private final List<Container> children = new ArrayList<>();
    private final Set<Object> unmanaged = Collections.newSetFromMap(new IdentityHashMap<>());
    private volatile boolean closed;

    private record Located(Container owner, Binding<?> binding) {
    }

    private record Pending(Key<?> key, Dependency<?> via) {
    }

    private Container(Container parent, Module[] modules) {
        this.parent = parent;
        Binder binder = new Binder();
        for (Module module : modules) {
            binder.install(module);
        }
        List<String> errors = new ArrayList<>();
        Map<Key<?>, Binding<?>> bindings = new LinkedHashMap<>();
        for (Binding<?> binding : binder.bindings(errors)) {
            Key<?> key = binding.key();
            Binding<?> existing = bindings.get(key);
            if (key.equals(SELF)) {
                errors.add("Container is bound automatically and cannot be rebound");
            } else if (existing != null) {
                errors.add("Duplicate binding for " + key + ": " + existing.describe() + " and " + binding.describe());
            } else if (parent != null && parent.findExplicit(key) != null) {
                errors.add(key + " is already bound in a parent container");
            } else {
                bindings.put(key, binding);
                if (binding instanceof InstanceBinding<?> instanceBinding) {
                    unmanaged.add(instanceBinding.instance());
                }
            }
        }
        this.explicit = Collections.unmodifiableMap(bindings);
        List<InterceptorBinding> allInterceptors = new ArrayList<>();
        if (parent != null) {
            allInterceptors.addAll(parent.interceptors);
        }
        allInterceptors.addAll(binder.interceptors());
        this.interceptors = List.copyOf(allInterceptors);
        if (errors.isEmpty()) {
            validate(errors);
        }
        if (!errors.isEmpty()) {
            throw new ConfigurationException(errors);
        }
    }

    public static Container create(Module... modules) {
        Container container = new Container(null, modules);
        container.startEagerSingletons();
        return container;
    }

    public Container createChild(Module... modules) {
        ensureOpen();
        Container child = new Container(this, modules);
        synchronized (lock) {
            ensureOpen();
            children.add(child);
        }
        child.startEagerSingletons();
        return child;
    }

    public Container parent() {
        return parent;
    }

    public <T> T getInstance(Class<T> type) {
        return getInstance(Key.of(type));
    }

    public <T> T getInstance(Key<T> key) {
        Objects.requireNonNull(key, "key");
        ensureOpen();
        if (key.equals(SELF)) {
            return key.type().cast(this);
        }
        Located located = locate(key);
        @SuppressWarnings("unchecked")
        Binding<T> binding = (Binding<T>) located.binding();
        return located.owner().provide(binding);
    }

    public <T> Provider<T> getProvider(Class<T> type) {
        return getProvider(Key.of(type));
    }

    public <T> Provider<T> getProvider(Key<T> key) {
        Objects.requireNonNull(key, "key");
        ensureOpen();
        if (!key.equals(SELF)) {
            locate(key);
        }
        return () -> getInstance(key);
    }

    public <T> T injectMembers(T instance) {
        Objects.requireNonNull(instance, "instance");
        ensureOpen();
        @SuppressWarnings("unchecked")
        Class<T> type = (Class<T>) instance.getClass();
        MembersInjector.forClass(type).inject(instance, this);
        return instance;
    }

    public boolean hasExplicitBinding(Key<?> key) {
        return findExplicit(key) != null;
    }

    public String explain(Class<?> type) {
        return explain(Key.of(type));
    }

    public String explain(Key<?> key) {
        ensureOpen();
        StringBuilder out = new StringBuilder();
        explain(this, key, false, 0, new HashSet<>(), out);
        return out.toString();
    }

    public boolean isClosed() {
        return closed;
    }

    @Override
    public void close() {
        List<Container> doomedChildren;
        List<Object> doomed;
        synchronized (lock) {
            if (closed) {
                return;
            }
            closed = true;
            doomedChildren = new ArrayList<>(children);
            children.clear();
            doomed = new ArrayList<>(destroyOrder);
            Collections.reverse(doomed);
            destroyOrder.clear();
            tracked.clear();
            singletons.clear();
        }
        if (parent != null) {
            parent.detach(this);
        }
        List<Throwable> failures = new ArrayList<>();
        for (int i = doomedChildren.size() - 1; i >= 0; i--) {
            try {
                doomedChildren.get(i).close();
            } catch (LifecycleException e) {
                Collections.addAll(failures, e.getSuppressed());
            }
        }
        for (Object instance : doomed) {
            for (Method method : Reflection.preDestroyMethods(instance.getClass())) {
                try {
                    Reflection.invoke(method, instance, NO_ARGUMENTS, "@PreDestroy " + Reflection.describe(method));
                } catch (RuntimeException e) {
                    failures.add(e);
                }
            }
        }
        if (!failures.isEmpty()) {
            LifecycleException failure = new LifecycleException(failures.size() + " @PreDestroy callback(s) failed");
            failures.forEach(failure::addSuppressed);
            throw failure;
        }
    }

    private void detach(Container child) {
        synchronized (lock) {
            children.remove(child);
        }
    }

    private void startEagerSingletons() {
        try {
            for (Binding<?> binding : explicit.values()) {
                if (binding.eager()) {
                    getInstance(binding.key());
                }
            }
        } catch (RuntimeException e) {
            try {
                close();
            } catch (LifecycleException suppressed) {
                e.addSuppressed(suppressed);
            }
            throw e;
        }
    }

    private void ensureOpen() {
        if (closed) {
            throw new IllegalStateException("Container is closed");
        }
    }

    private Binding<?> findExplicit(Key<?> key) {
        for (Container current = this; current != null; current = current.parent) {
            Binding<?> binding = current.explicit.get(key);
            if (binding != null) {
                return binding;
            }
        }
        return null;
    }

    private Located locate(Key<?> key) {
        for (Container current = this; current != null; current = current.parent) {
            Binding<?> binding = current.explicit.get(key);
            if (binding != null) {
                return new Located(current, binding);
            }
        }
        for (Container current = this; current != null; current = current.parent) {
            Binding<?> binding = current.justInTime.get(key);
            if (binding != null) {
                return new Located(current, binding);
            }
        }
        return new Located(this, justInTime.computeIfAbsent(key, missing -> ConstructorBinding.justInTime(missing)));
    }

    private <T> T provide(Binding<T> binding) {
        ensureOpen();
        Key<T> key = binding.key();
        ArrayDeque<Key<?>> stack = RESOLVING.get();
        if (stack.contains(key)) {
            List<String> path = new ArrayList<>();
            boolean inCycle = false;
            for (Key<?> entry : stack) {
                inCycle = inCycle || entry.equals(key);
                if (inCycle) {
                    path.add(entry.toString());
                }
            }
            path.add(key.toString());
            throw new CircularDependencyException(path);
        }
        stack.addLast(key);
        try {
            return binding.scope() == Scope.SINGLETON ? singleton(binding) : instantiate(binding);
        } finally {
            stack.removeLast();
            if (stack.isEmpty()) {
                RESOLVING.remove();
            }
        }
    }

    private <T> T singleton(Binding<T> binding) {
        synchronized (lock) {
            ensureOpen();
            Object existing = singletons.get(binding.key());
            if (existing != null) {
                return binding.key().type().cast(existing);
            }
            T created = instantiate(binding);
            singletons.put(binding.key(), created);
            if (binding.managesLifecycle()) {
                track(created);
            }
            return created;
        }
    }

    private void track(Object instance) {
        Object target = InterceptingHandler.unwrap(instance);
        for (Container current = this; current != null; current = current.parent) {
            if (current.unmanaged.contains(target)) {
                return;
            }
        }
        if (!Reflection.preDestroyMethods(target.getClass()).isEmpty() && tracked.add(target)) {
            destroyOrder.add(target);
        }
    }

    private <T> T instantiate(Binding<T> binding) {
        return intercept(binding.key().type(), binding.provision(this));
    }

    private <T> T intercept(Class<T> type, T instance) {
        if (!type.isInterface() || interceptors.isEmpty() || InterceptingHandler.isProxy(instance)) {
            return instance;
        }
        Class<?> implementation = instance.getClass();
        Map<Method, List<MethodInterceptor>> chains = new HashMap<>();
        for (Method method : type.getMethods()) {
            Method concrete = implementationMethod(implementation, method);
            List<MethodInterceptor> chain = new ArrayList<>();
            for (InterceptorBinding interceptor : interceptors) {
                if (interceptor.classes().matches(implementation) && interceptor.methods().matches(concrete)) {
                    chain.addAll(interceptor.chain());
                }
            }
            if (!chain.isEmpty()) {
                chains.put(method, List.copyOf(chain));
            }
        }
        if (chains.isEmpty()) {
            return instance;
        }
        return type.cast(InterceptingHandler.proxy(type, instance, chains));
    }

    private static Method implementationMethod(Class<?> implementation, Method method) {
        for (Class<?> current = implementation; current != null; current = current.getSuperclass()) {
            try {
                return current.getDeclaredMethod(method.getName(), method.getParameterTypes());
            } catch (NoSuchMethodException e) {
                continue;
            }
        }
        return method;
    }

    private void validate(List<String> errors) {
        Map<Key<?>, Boolean> finished = new HashMap<>();
        Deque<Pending> roots = new ArrayDeque<>();
        for (Key<?> key : explicit.keySet()) {
            roots.add(new Pending(key, null));
        }
        while (!roots.isEmpty()) {
            Pending next = roots.poll();
            visit(next.key(), next.via(), new ArrayList<>(), finished, roots, errors);
        }
    }

    private void visit(Key<?> key, Dependency<?> via, List<Key<?>> path, Map<Key<?>, Boolean> finished,
                       Deque<Pending> roots, List<String> errors) {
        if (key.equals(SELF)) {
            return;
        }
        Boolean state = finished.get(key);
        if (Boolean.TRUE.equals(state)) {
            return;
        }
        if (Boolean.FALSE.equals(state)) {
            List<String> cycle = new ArrayList<>();
            for (Key<?> entry : path.subList(path.indexOf(key), path.size())) {
                cycle.add(entry.toString());
            }
            cycle.add(key.toString());
            errors.add("Circular dependency: " + String.join(" -> ", cycle) + " (inject a Provider to break it)");
            return;
        }
        Located located;
        try {
            located = locate(key);
        } catch (ConfigurationException e) {
            finished.put(key, true);
            for (String message : e.messages()) {
                errors.add(via == null ? message : message + " (required by " + via.site() + ")");
            }
            return;
        }
        if (located.owner() != this) {
            finished.put(key, true);
            return;
        }
        finished.put(key, false);
        path.add(key);
        for (Dependency<?> dependency : located.binding().dependencies()) {
            if (dependency.deferred()) {
                if (!finished.containsKey(dependency.key())) {
                    roots.add(new Pending(dependency.key(), dependency));
                }
            } else {
                visit(dependency.key(), dependency, path, finished, roots, errors);
            }
        }
        path.remove(path.size() - 1);
        finished.put(key, true);
    }

    private static void explain(Container from, Key<?> key, boolean deferred, int depth, Set<Key<?>> expanded,
                                StringBuilder out) {
        out.append("  ".repeat(depth)).append(deferred ? "Provider<" + key + ">" : key.toString());
        if (key.equals(SELF)) {
            out.append(" [container]").append(System.lineSeparator());
            return;
        }
        Located located;
        try {
            located = from.locate(key);
        } catch (ConfigurationException e) {
            out.append(" [unresolvable: ").append(e.getMessage()).append(']').append(System.lineSeparator());
            return;
        }
        Binding<?> binding = located.binding();
        out.append(" [").append(binding.scope().name().toLowerCase(Locale.ROOT)).append(", ").append(binding.describe());
        if (located.owner() != from) {
            out.append(", inherited");
        }
        out.append(']');
        List<Dependency<?>> dependencies = binding.dependencies();
        if (deferred) {
            out.append(" deferred").append(System.lineSeparator());
            return;
        }
        if (!dependencies.isEmpty() && !expanded.add(key)) {
            out.append(" (see above)").append(System.lineSeparator());
            return;
        }
        out.append(System.lineSeparator());
        for (Dependency<?> dependency : dependencies) {
            explain(located.owner(), dependency.key(), dependency.deferred(), depth + 1, expanded, out);
        }
    }
}
