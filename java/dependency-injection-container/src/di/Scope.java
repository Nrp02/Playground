package di;

public enum Scope {
    SINGLETON,
    PROTOTYPE;

    static Scope declaredOn(Class<?> type) {
        return type.isAnnotationPresent(Singleton.class) ? SINGLETON : PROTOTYPE;
    }
}
