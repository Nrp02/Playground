package di;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

final class ConstructorBinding<T> extends Binding<T> {
    private final Class<T> implementation;
    private final Constructor<T> constructor;
    private final List<Dependency<?>> parameters;
    private final MembersInjector<T> members;

    private ConstructorBinding(Key<T> key, Class<T> implementation, Constructor<T> constructor,
                               List<Dependency<?>> parameters, MembersInjector<T> members, Scope scope, boolean eager) {
        super(key, scope, eager);
        this.implementation = implementation;
        this.constructor = constructor;
        this.parameters = parameters;
        this.members = members;
    }

    static <T> ConstructorBinding<T> justInTime(Key<T> key) {
        if (key.qualifier() != null) {
            throw new ConfigurationException("No binding for " + key);
        }
        return create(key, key.type(), null, false);
    }

    static <T> ConstructorBinding<T> create(Key<T> key, Class<T> implementation, Scope scope, boolean eager) {
        String name = Reflection.name(implementation);
        if (implementation.isInterface()) {
            throw new ConfigurationException("No implementation bound for interface " + key);
        }
        if (implementation.isPrimitive() || implementation.isArray() || implementation.isEnum()
            || implementation.getName().startsWith("java.")) {
            throw new ConfigurationException("No binding for " + key + "; it must be bound explicitly");
        }
        if (Modifier.isAbstract(implementation.getModifiers())) {
            throw new ConfigurationException("No implementation bound for abstract class " + key);
        }
        if (implementation.isAnonymousClass()
            || (implementation.isMemberClass() && !Modifier.isStatic(implementation.getModifiers()))) {
            throw new ConfigurationException(name + " is an inner class and cannot be constructed");
        }

        List<String> errors = new ArrayList<>();
        Constructor<T> constructor = null;
        List<Dependency<?>> parameters = List.of();
        try {
            constructor = select(implementation);
            parameters = Dependency.forExecutable(constructor);
        } catch (ConfigurationException e) {
            errors.addAll(e.messages());
        }
        MembersInjector<T> members = null;
        try {
            members = MembersInjector.forClass(implementation);
        } catch (ConfigurationException e) {
            errors.addAll(e.messages());
        }
        if (!errors.isEmpty()) {
            throw new ConfigurationException(errors);
        }
        Scope effective = scope != null ? scope : Scope.declaredOn(implementation);
        return new ConstructorBinding<>(key, implementation, constructor, parameters, members, effective, eager);
    }

    private static <T> Constructor<T> select(Class<T> implementation) {
        String name = Reflection.name(implementation);
        Constructor<?> annotated = null;
        for (Constructor<?> candidate : implementation.getDeclaredConstructors()) {
            if (!candidate.isAnnotationPresent(Inject.class)) {
                continue;
            }
            if (annotated != null) {
                throw new ConfigurationException(name + " has more than one @Inject constructor");
            }
            annotated = candidate;
        }
        try {
            Constructor<T> chosen = annotated != null
                ? implementation.getDeclaredConstructor(annotated.getParameterTypes())
                : implementation.getDeclaredConstructor();
            chosen.setAccessible(true);
            return chosen;
        } catch (NoSuchMethodException e) {
            throw new ConfigurationException(name + " needs an @Inject constructor or a no-argument constructor");
        }
    }

    @Override
    List<Dependency<?>> dependencies() {
        List<Dependency<?>> all = new ArrayList<>(parameters);
        all.addAll(members.dependencies());
        return all;
    }

    @Override
    T provision(Container container) {
        Object[] arguments = new Object[parameters.size()];
        for (int i = 0; i < arguments.length; i++) {
            arguments[i] = parameters.get(i).resolve(container);
        }
        T instance;
        try {
            instance = constructor.newInstance(arguments);
        } catch (InvocationTargetException e) {
            throw Reflection.propagate(Reflection.describe(constructor), e.getCause());
        } catch (InstantiationException | IllegalAccessException e) {
            throw new ProvisionException("Cannot instantiate " + Reflection.name(implementation), e);
        }
        members.inject(instance, container);
        members.postConstruct(instance);
        return instance;
    }

    @Override
    String describe() {
        return "constructor " + Reflection.name(implementation);
    }
}
