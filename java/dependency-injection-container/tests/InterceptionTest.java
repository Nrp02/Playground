import di.Container;
import di.Matchers;
import di.MethodInterceptor;
import di.Scope;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

public final class InterceptionTest {

    @Retention(RetentionPolicy.RUNTIME)
    @Target(ElementType.METHOD)
    @interface Audited {
    }

    @Retention(RetentionPolicy.RUNTIME)
    @Target(ElementType.TYPE)
    @interface Remote {
    }

    interface Calculator {
        int add(int a, int b);

        int flaky();

        String name();

        default String describe() {
            return "calculator " + name();
        }
    }

    static final class SimpleCalculator implements Calculator {
        private int attempts;

        @Audited
        @Override
        public int add(int a, int b) {
            return a + b;
        }

        @Audited
        @Override
        public int flaky() {
            attempts++;
            if (attempts < 3) {
                throw new IllegalStateException("attempt " + attempts);
            }
            return attempts;
        }

        @Override
        public String name() {
            return "simple";
        }

        @Override
        public String toString() {
            return "SimpleCalculator";
        }
    }

    @Remote
    static final class RemoteCalculator implements Calculator {
        @Override
        public int add(int a, int b) {
            return a + b;
        }

        @Override
        public int flaky() {
            throw new UnsupportedOperationException("remote failure");
        }

        @Override
        public String name() {
            return "remote";
        }
    }

    static MethodInterceptor recording(List<String> log, String label) {
        return invocation -> {
            log.add(label + " before " + invocation.method().getName());
            Object result = invocation.proceed();
            log.add(label + " after " + result);
            return result;
        };
    }

    private InterceptionTest() {
    }

    public static void run() {
        Assertions.suite("Interception");

        Assertions.test("interceptors wrap matching methods in registration order", () -> {
            List<String> log = new ArrayList<>();
            try (Container container = Container.create(binder -> {
                binder.bind(Calculator.class).to(SimpleCalculator.class);
                binder.bindInterceptor(Matchers.any(), Matchers.annotatedWith(Audited.class),
                    recording(log, "outer"), recording(log, "inner"));
            })) {
                Calculator calculator = container.getInstance(Calculator.class);
                Assertions.assertEquals("result", 5, calculator.add(2, 3));
                Assertions.assertEquals("log", List.of("outer before add", "inner before add", "inner after 5", "outer after 5"), log);
                log.clear();
                Assertions.assertEquals("unmatched", "simple", calculator.name());
                Assertions.assertEquals("no log", List.of(), log);
            }
        });

        Assertions.test("an interceptor can proceed more than once", () -> {
            try (Container container = Container.create(binder -> {
                binder.bind(Calculator.class).to(SimpleCalculator.class).in(Scope.SINGLETON);
                binder.bindInterceptor(Matchers.any(), Matchers.named("flaky"), invocation -> {
                    for (int attempt = 1; ; attempt++) {
                        try {
                            return invocation.proceed();
                        } catch (IllegalStateException e) {
                            if (attempt == 5) {
                                throw e;
                            }
                        }
                    }
                });
            })) {
                Assertions.assertEquals("retried", 3, container.getInstance(Calculator.class).flaky());
            }
        });

        Assertions.test("target exceptions propagate unwrapped", () -> {
            try (Container container = Container.create(binder -> {
                binder.bind(Calculator.class).to(RemoteCalculator.class);
                binder.bindInterceptor(Matchers.any(), Matchers.any(), invocation -> invocation.proceed());
            })) {
                Calculator calculator = container.getInstance(Calculator.class);
                Assertions.assertThrows("unwrapped", UnsupportedOperationException.class, calculator::flaky);
            }
        });

        Assertions.test("class matchers select implementations", () -> {
            List<String> log = new ArrayList<>();
            try (Container container = Container.create(binder -> {
                binder.bind(Calculator.class).to(RemoteCalculator.class);
                binder.bind(Calculator.class).named("local").to(SimpleCalculator.class);
                binder.bindInterceptor(Matchers.annotatedWith(Remote.class), Matchers.returning(int.class), recording(log, "remote"));
            })) {
                container.getInstance(Calculator.class).add(1, 1);
                Assertions.assertEquals("remote logged", 2, log.size());
                Calculator local = container.getInstance(di.Key.named(Calculator.class, "local"));
                Assertions.assertFalse("local not proxied", Proxy.isProxyClass(local.getClass()));
            }
        });

        Assertions.test("Object methods bypass interceptors and use proxy identity", () -> {
            List<String> log = new ArrayList<>();
            try (Container container = Container.create(binder -> {
                binder.bind(Calculator.class).to(SimpleCalculator.class);
                binder.bindInterceptor(Matchers.any(), Matchers.any(), recording(log, "all"));
            })) {
                Calculator calculator = container.getInstance(Calculator.class);
                Assertions.assertEquals("toString delegates", "SimpleCalculator", calculator.toString());
                Assertions.assertTrue("equals self", calculator.equals(calculator));
                Assertions.assertEquals("hash", System.identityHashCode(calculator), calculator.hashCode());
                Assertions.assertEquals("no interception", List.of(), log);
            }
        });

        Assertions.test("default interface methods are intercepted and call through the proxy", () -> {
            List<String> log = new ArrayList<>();
            try (Container container = Container.create(binder -> {
                binder.bind(Calculator.class).to(SimpleCalculator.class);
                binder.bindInterceptor(Matchers.any(), Matchers.named("describe"), recording(log, "d"));
            })) {
                Assertions.assertEquals("value", "calculator simple", container.getInstance(Calculator.class).describe());
                Assertions.assertEquals("log", List.of("d before describe", "d after calculator simple"), log);
            }
        });

        Assertions.test("concrete keys are never proxied", () -> {
            List<String> log = new ArrayList<>();
            try (Container container = Container.create(binder ->
                binder.bindInterceptor(Matchers.any(), Matchers.any(), recording(log, "x")))) {
                SimpleCalculator calculator = container.getInstance(SimpleCalculator.class);
                Assertions.assertEquals("plain", 4, calculator.add(2, 2));
                Assertions.assertEquals("no log", List.of(), log);
            }
        });

        Assertions.test("matcher combinators", () -> {
            di.Matcher<Class<?>> remote = Matchers.annotatedWith(Remote.class);
            di.Matcher<Class<?>> calculators = Matchers.subclassesOf(Calculator.class);
            Assertions.assertTrue("and", calculators.and(remote).matches(RemoteCalculator.class));
            Assertions.assertFalse("and fails", calculators.and(remote).matches(SimpleCalculator.class));
            Assertions.assertTrue("or", remote.or(calculators).matches(SimpleCalculator.class));
            Assertions.assertTrue("negate", remote.negate().matches(SimpleCalculator.class));
            Assertions.assertTrue("package", Matchers.inPackage("").matches(SimpleCalculator.class));
        });
    }
}
