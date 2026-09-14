package di;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;

public final class Invocation {
    private final Object target;
    private final Method method;
    private final Object[] arguments;
    private final List<MethodInterceptor> chain;
    private final int index;

    Invocation(Object target, Method method, Object[] arguments, List<MethodInterceptor> chain, int index) {
        this.target = target;
        this.method = method;
        this.arguments = arguments;
        this.chain = chain;
        this.index = index;
    }

    public Object target() {
        return target;
    }

    public Method method() {
        return method;
    }

    public Object[] arguments() {
        return arguments.clone();
    }

    public Object proceed() throws Throwable {
        if (index < chain.size()) {
            return chain.get(index).invoke(new Invocation(target, method, arguments, chain, index + 1));
        }
        if (!method.canAccess(target)) {
            method.setAccessible(true);
        }
        try {
            return method.invoke(target, arguments);
        } catch (InvocationTargetException e) {
            throw e.getCause();
        }
    }

    @Override
    public String toString() {
        return Reflection.name(target.getClass()) + "." + method.getName();
    }
}
