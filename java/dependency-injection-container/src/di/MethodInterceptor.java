package di;

@FunctionalInterface
public interface MethodInterceptor {
    Object invoke(Invocation invocation) throws Throwable;
}
