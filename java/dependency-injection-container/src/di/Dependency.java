package di;

import java.lang.annotation.Annotation;
import java.lang.reflect.Executable;
import java.lang.reflect.Field;
import java.lang.reflect.Parameter;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;

final class Dependency<T> {
    private final Key<T> key;
    private final boolean deferred;
    private final String site;

    Dependency(Key<T> key, boolean deferred, String site) {
        this.key = key;
        this.deferred = deferred;
        this.site = site;
    }

    Key<T> key() {
        return key;
    }

    boolean deferred() {
        return deferred;
    }

    String site() {
        return site;
    }

    Object resolve(Container container) {
        return deferred ? container.getProvider(key) : container.getInstance(key);
    }

    static List<Dependency<?>> forExecutable(Executable executable) {
        List<Dependency<?>> dependencies = new ArrayList<>();
        List<String> errors = new ArrayList<>();
        Parameter[] parameters = executable.getParameters();
        for (int i = 0; i < parameters.length; i++) {
            Parameter parameter = parameters[i];
            String site = "parameter " + i + " of " + Reflection.describe(executable);
            try {
                dependencies.add(create(parameter.getType(), parameter.getParameterizedType(),
                    parameter.getAnnotations(), site));
            } catch (ConfigurationException e) {
                errors.addAll(e.messages());
            }
        }
        if (!errors.isEmpty()) {
            throw new ConfigurationException(errors);
        }
        return List.copyOf(dependencies);
    }

    static Dependency<?> forField(Field field) {
        return create(field.getType(), field.getGenericType(), field.getAnnotations(), "field " + Reflection.describe(field));
    }

    static Annotation qualifier(Annotation[] annotations, String site) {
        Annotation found = null;
        for (Annotation annotation : annotations) {
            if (!annotation.annotationType().isAnnotationPresent(Qualifier.class)) {
                continue;
            }
            if (found != null) {
                throw new ConfigurationException(site + " has more than one qualifier");
            }
            found = annotation;
        }
        return found;
    }

    private static Dependency<?> create(Class<?> raw, Type generic, Annotation[] annotations, String site) {
        Annotation qualifier = qualifier(annotations, site);
        if (raw == Provider.class) {
            if (generic instanceof ParameterizedType parameterized
                && parameterized.getActualTypeArguments()[0] instanceof Class<?> argument) {
                return new Dependency<>(Key.of(argument, qualifier), true, site);
            }
            throw new ConfigurationException(site + ": Provider must be parameterized with a concrete class");
        }
        if (generic instanceof ParameterizedType) {
            throw new ConfigurationException(site + ": generic type " + generic.getTypeName()
                + " cannot be injected, bind a concrete type instead");
        }
        return new Dependency<>(Key.of(raw, qualifier), false, site);
    }
}
