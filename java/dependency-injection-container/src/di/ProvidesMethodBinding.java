package di;

import java.lang.annotation.Annotation;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.TypeVariable;
import java.util.List;

final class ProvidesMethodBinding<T> extends Binding<T> {
    private final Object module;
    private final Method method;
    private final List<Dependency<?>> parameters;

    private ProvidesMethodBinding(Key<T> key, Object module, Method method, List<Dependency<?>> parameters, Scope scope) {
        super(key, scope, false);
        this.module = module;
        this.method = method;
        this.parameters = parameters;
    }

    static Binding<?> create(Object module, Method method) {
        String site = "@Provides " + Reflection.describe(method);
        Class<?> returnType = method.getReturnType();
        if (returnType == void.class) {
            throw new ConfigurationException(site + " must return a value");
        }
        if (method.getGenericReturnType() instanceof ParameterizedType
            || method.getGenericReturnType() instanceof TypeVariable<?>) {
            throw new ConfigurationException(site + " must return a non-generic type");
        }
        Annotation qualifier = Dependency.qualifier(method.getAnnotations(), site);
        Scope scope = method.isAnnotationPresent(Singleton.class) ? Scope.SINGLETON : Scope.PROTOTYPE;
        return build(Key.of(returnType, qualifier), module, method, scope);
    }

    private static <T> ProvidesMethodBinding<T> build(Key<T> key, Object module, Method method, Scope scope) {
        List<Dependency<?>> parameters = Dependency.forExecutable(method);
        method.setAccessible(true);
        Object target = Modifier.isStatic(method.getModifiers()) ? null : module;
        return new ProvidesMethodBinding<>(key, target, method, parameters, scope);
    }

    @Override
    List<Dependency<?>> dependencies() {
        return parameters;
    }

    @Override
    T provision(Container container) {
        Object[] arguments = new Object[parameters.size()];
        for (int i = 0; i < arguments.length; i++) {
            arguments[i] = parameters.get(i).resolve(container);
        }
        String site = "@Provides " + Reflection.describe(method);
        Object result = Reflection.invoke(method, module, arguments, site);
        if (result == null) {
            throw new ProvisionException(site + " returned null");
        }
        return key().type().cast(result);
    }

    @Override
    String describe() {
        return "@Provides " + Reflection.describe(method);
    }
}
