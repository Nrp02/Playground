package di;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.List;
import java.util.Map;

final class InterceptingHandler implements InvocationHandler {
    private static final Object[] NO_ARGUMENTS = new Object[0];

    private final Object target;
    private final Map<Method, List<MethodInterceptor>> chains;

    private InterceptingHandler(Object target, Map<Method, List<MethodInterceptor>> chains) {
        this.target = target;
        this.chains = chains;
    }

    static Object proxy(Class<?> type, Object target, Map<Method, List<MethodInterceptor>> chains) {
        ClassLoader loader = type.getClassLoader() != null ? type.getClassLoader() : target.getClass().getClassLoader();
        return Proxy.newProxyInstance(loader, new Class<?>[] {type}, new InterceptingHandler(target, Map.copyOf(chains)));
    }

    static boolean isProxy(Object candidate) {
        return Proxy.isProxyClass(candidate.getClass())
            && Proxy.getInvocationHandler(candidate) instanceof InterceptingHandler;
    }

    static Object unwrap(Object candidate) {
        return isProxy(candidate) ? ((InterceptingHandler) Proxy.getInvocationHandler(candidate)).target : candidate;
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        Object[] arguments = args == null ? NO_ARGUMENTS : args;
        if (method.getDeclaringClass() == Object.class) {
            return switch (method.getName()) {
                case "equals" -> proxy == arguments[0];
                case "hashCode" -> System.identityHashCode(proxy);
                default -> target.toString();
            };
        }
        List<MethodInterceptor> chain = chains.getOrDefault(method, List.of());
        return new Invocation(target, method, arguments, chain, 0).proceed();
    }
}
