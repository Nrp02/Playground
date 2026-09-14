package di;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

final class MembersInjector<T> {
    private static final Object[] NO_ARGUMENTS = new Object[0];

    private final List<Field> fields;
    private final List<Dependency<?>> fieldDependencies;
    private final List<Method> methods;
    private final List<List<Dependency<?>>> methodDependencies;
    private final List<Method> postConstruct;

    private MembersInjector(List<Field> fields, List<Dependency<?>> fieldDependencies, List<Method> methods,
                            List<List<Dependency<?>>> methodDependencies, List<Method> postConstruct) {
        this.fields = fields;
        this.fieldDependencies = fieldDependencies;
        this.methods = methods;
        this.methodDependencies = methodDependencies;
        this.postConstruct = postConstruct;
    }

    static <T> MembersInjector<T> forClass(Class<T> type) {
        List<String> errors = new ArrayList<>();
        List<Field> fields = new ArrayList<>();
        List<Dependency<?>> fieldDependencies = new ArrayList<>();
        for (Class<?> current : Reflection.hierarchy(type)) {
            Field[] declared = current.getDeclaredFields();
            Arrays.sort(declared, Comparator.comparing(Field::getName));
            for (Field field : declared) {
                if (!field.isAnnotationPresent(Inject.class)) {
                    continue;
                }
                int modifiers = field.getModifiers();
                if (Modifier.isStatic(modifiers)) {
                    errors.add("field " + Reflection.describe(field) + ": static fields cannot be injected");
                    continue;
                }
                if (Modifier.isFinal(modifiers)) {
                    errors.add("field " + Reflection.describe(field) + ": final fields cannot be injected");
                    continue;
                }
                try {
                    fieldDependencies.add(Dependency.forField(field));
                    field.setAccessible(true);
                    fields.add(field);
                } catch (ConfigurationException e) {
                    errors.addAll(e.messages());
                }
            }
        }

        List<Method> methods = new ArrayList<>();
        List<List<Dependency<?>>> methodDependencies = new ArrayList<>();
        for (Method method : Reflection.annotatedMethods(type, Inject.class)) {
            if (Modifier.isStatic(method.getModifiers())) {
                errors.add(Reflection.describe(method) + ": static methods cannot be injected");
                continue;
            }
            try {
                methodDependencies.add(Dependency.forExecutable(method));
                method.setAccessible(true);
                methods.add(method);
            } catch (ConfigurationException e) {
                errors.addAll(e.messages());
            }
        }

        List<Method> postConstruct = List.of();
        try {
            postConstruct = Reflection.callbacks(type, PostConstruct.class);
        } catch (ConfigurationException e) {
            errors.addAll(e.messages());
        }
        try {
            Reflection.preDestroyMethods(type);
        } catch (ConfigurationException e) {
            errors.addAll(e.messages());
        }

        if (!errors.isEmpty()) {
            throw new ConfigurationException(errors);
        }
        return new MembersInjector<>(List.copyOf(fields), List.copyOf(fieldDependencies), List.copyOf(methods),
            List.copyOf(methodDependencies), postConstruct);
    }

    List<Dependency<?>> dependencies() {
        List<Dependency<?>> all = new ArrayList<>(fieldDependencies);
        methodDependencies.forEach(all::addAll);
        return all;
    }

    void inject(T instance, Container container) {
        for (int i = 0; i < fields.size(); i++) {
            Field field = fields.get(i);
            Object value = fieldDependencies.get(i).resolve(container);
            try {
                field.set(instance, value);
            } catch (IllegalAccessException e) {
                throw new ProvisionException("field " + Reflection.describe(field) + " is not accessible", e);
            }
        }
        for (int i = 0; i < methods.size(); i++) {
            List<Dependency<?>> dependencies = methodDependencies.get(i);
            Object[] arguments = new Object[dependencies.size()];
            for (int j = 0; j < arguments.length; j++) {
                arguments[j] = dependencies.get(j).resolve(container);
            }
            Method method = methods.get(i);
            Reflection.invoke(method, instance, arguments, "@Inject method " + Reflection.describe(method));
        }
    }

    void postConstruct(T instance) {
        for (Method method : postConstruct) {
            Reflection.invoke(method, instance, NO_ARGUMENTS, "@PostConstruct " + Reflection.describe(method));
        }
    }
}
