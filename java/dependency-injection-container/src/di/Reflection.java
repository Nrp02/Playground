package di;

import java.lang.annotation.Annotation;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

final class Reflection {
    private static final ClassValue<List<Method>> PRE_DESTROY = new ClassValue<>() {
        @Override
        protected List<Method> computeValue(Class<?> type) {
            return callbacks(type, PreDestroy.class);
        }
    };

    private Reflection() {
    }

    static List<Class<?>> hierarchy(Class<?> type) {
        ArrayDeque<Class<?>> chain = new ArrayDeque<>();
        for (Class<?> current = type; current != null && current != Object.class; current = current.getSuperclass()) {
            chain.addFirst(current);
        }
        return List.copyOf(chain);
    }

    static List<Method> annotatedMethods(Class<?> type, Class<? extends Annotation> annotation) {
        List<Method> found = new ArrayList<>();
        Set<String> overridden = new HashSet<>();
        for (Class<?> current = type; current != null && current != Object.class; current = current.getSuperclass()) {
            Method[] declared = current.getDeclaredMethods();
            Arrays.sort(declared, Comparator.comparing(Method::getName).thenComparingInt(Method::getParameterCount));
            List<Method> level = new ArrayList<>();
            for (Method method : declared) {
                if (method.isBridge() || method.isSynthetic()) {
                    continue;
                }
                int modifiers = method.getModifiers();
                boolean overridable = !Modifier.isPrivate(modifiers) && !Modifier.isStatic(modifiers);
                String signature = method.getName() + Arrays.toString(method.getParameterTypes());
                if (overridable && !overridden.add(signature)) {
                    continue;
                }
                if (method.isAnnotationPresent(annotation)) {
                    level.add(method);
                }
            }
            found.addAll(0, level);
        }
        return found;
    }

    static List<Method> callbacks(Class<?> type, Class<? extends Annotation> annotation) {
        List<String> errors = new ArrayList<>();
        List<Method> methods = annotatedMethods(type, annotation);
        for (Method method : methods) {
            if (Modifier.isStatic(method.getModifiers())) {
                errors.add(describe(method) + ": @" + annotation.getSimpleName() + " methods must not be static");
            } else if (method.getParameterCount() != 0) {
                errors.add(describe(method) + ": @" + annotation.getSimpleName() + " methods must not take parameters");
            } else {
                method.setAccessible(true);
            }
        }
        if (!errors.isEmpty()) {
            throw new ConfigurationException(errors);
        }
        return List.copyOf(methods);
    }

    static List<Method> preDestroyMethods(Class<?> type) {
        return PRE_DESTROY.get(type);
    }

    static Object invoke(Method method, Object target, Object[] arguments, String context) {
        try {
            return method.invoke(target, arguments);
        } catch (InvocationTargetException e) {
            throw propagate(context, e.getCause());
        } catch (IllegalAccessException e) {
            throw new ProvisionException(context + " is not accessible", e);
        }
    }

    static RuntimeException propagate(String context, Throwable cause) {
        if (cause instanceof DiException known) {
            return known;
        }
        return new ProvisionException(context + " threw " + cause, cause);
    }

    static String describe(Executable executable) {
        String owner = name(executable.getDeclaringClass());
        if (executable instanceof Constructor<?>) {
            return owner + " constructor";
        }
        return owner + "." + executable.getName() + "()";
    }

    static String describe(Field field) {
        return name(field.getDeclaringClass()) + "." + field.getName();
    }

    static String name(Class<?> type) {
        String simple = type.getSimpleName();
        return simple.isEmpty() ? type.getName() : simple;
    }
}
