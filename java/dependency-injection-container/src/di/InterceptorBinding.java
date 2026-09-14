package di;

import java.lang.reflect.Method;
import java.util.List;

record InterceptorBinding(Matcher<? super Class<?>> classes, Matcher<? super Method> methods,
                          List<MethodInterceptor> chain) {
}
