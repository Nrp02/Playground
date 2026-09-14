package di;

import java.lang.annotation.Annotation;
import java.lang.reflect.AnnotatedElement;
import java.lang.reflect.Method;
import java.util.Objects;

public final class Matchers {

    private Matchers() {
    }

    public static <T> Matcher<T> any() {
        return candidate -> true;
    }

    public static <T extends AnnotatedElement> Matcher<T> annotatedWith(Class<? extends Annotation> annotation) {
        Objects.requireNonNull(annotation, "annotation");
        return candidate -> candidate.isAnnotationPresent(annotation);
    }

    public static Matcher<Class<?>> subclassesOf(Class<?> base) {
        Objects.requireNonNull(base, "base");
        return base::isAssignableFrom;
    }

    public static Matcher<Class<?>> inPackage(String packageName) {
        return candidate -> candidate.getPackageName().equals(packageName);
    }

    public static Matcher<Method> named(String name) {
        return candidate -> candidate.getName().equals(name);
    }

    public static Matcher<Method> returning(Class<?> type) {
        return candidate -> type.isAssignableFrom(candidate.getReturnType());
    }
}
