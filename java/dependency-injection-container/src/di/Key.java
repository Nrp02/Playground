package di;

import java.lang.annotation.Annotation;
import java.util.Map;
import java.util.Objects;

public final class Key<T> {
    private static final Map<Class<?>, Class<?>> WRAPPERS = Map.of(
        boolean.class, Boolean.class,
        byte.class, Byte.class,
        short.class, Short.class,
        char.class, Character.class,
        int.class, Integer.class,
        long.class, Long.class,
        float.class, Float.class,
        double.class, Double.class);

    private final Class<T> type;
    private final Annotation qualifier;

    private Key(Class<T> type, Annotation qualifier) {
        this.type = type;
        this.qualifier = qualifier;
    }

    public static <T> Key<T> of(Class<T> type) {
        return of(type, null);
    }

    public static <T> Key<T> of(Class<T> type, Annotation qualifier) {
        Objects.requireNonNull(type, "type");
        if (type == void.class || type == Void.class) {
            throw new ConfigurationException("void cannot be used as a binding key");
        }
        if (qualifier != null && !qualifier.annotationType().isAnnotationPresent(Qualifier.class)) {
            throw new ConfigurationException("@" + qualifier.annotationType().getSimpleName()
                + " is not annotated with @Qualifier");
        }
        return new Key<>(wrap(type), qualifier);
    }

    public static <T> Key<T> named(Class<T> type, String name) {
        return of(type, Names.named(name));
    }

    public Class<T> type() {
        return type;
    }

    public Annotation qualifier() {
        return qualifier;
    }

    @SuppressWarnings("unchecked")
    private static <T> Class<T> wrap(Class<T> type) {
        Class<?> wrapper = WRAPPERS.get(type);
        return wrapper == null ? type : (Class<T>) wrapper;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof Key<?> key && type.equals(key.type) && Objects.equals(qualifier, key.qualifier);
    }

    @Override
    public int hashCode() {
        return 31 * type.hashCode() + Objects.hashCode(qualifier);
    }

    @Override
    public String toString() {
        String name = Reflection.name(type);
        if (qualifier == null) {
            return name;
        }
        if (qualifier instanceof Named named) {
            return "@Named(\"" + named.value() + "\") " + name;
        }
        return "@" + qualifier.annotationType().getSimpleName() + " " + name;
    }
}
