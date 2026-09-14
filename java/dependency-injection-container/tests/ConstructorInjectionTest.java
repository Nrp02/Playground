import di.ConfigurationException;
import di.Container;
import di.Inject;
import di.Named;
import di.ProvisionException;
import di.Singleton;
import java.util.ArrayList;
import java.util.List;

public final class ConstructorInjectionTest {

    static final class Engine {
    }

    static final class Car {
        final Engine engine;

        @Inject
        Car(Engine engine) {
            this.engine = engine;
        }
    }

    @Singleton
    static final class Registry {
    }

    static final class TwoInjectConstructors {
        @Inject
        TwoInjectConstructors() {
        }

        @Inject
        TwoInjectConstructors(Engine engine) {
        }
    }

    static final class NoUsableConstructor {
        NoUsableConstructor(String value) {
        }
    }

    abstract static class AbstractThing {
    }

    final class InnerClass {
    }

    static class Base {
        final List<String> events = new ArrayList<>();

        @Inject
        Engine baseEngine;

        @Inject
        void initBase(Engine engine) {
            events.add("base-method engine=" + (engine != null) + " field=" + (baseEngine != null));
        }

        @Inject
        void overridden(Engine engine) {
            events.add("base-overridden");
        }
    }

    static final class Derived extends Base {
        @Inject
        Registry registry;

        @Inject
        Derived() {
            events.add("constructor");
        }

        @Inject
        void initDerived(Registry value) {
            events.add("derived-method registry=" + (registry != null));
        }

        @Override
        void overridden(Engine engine) {
            events.add("derived-overridden");
        }
    }

    static final class FinalField {
        @Inject
        final Engine engine = null;
    }

    static final class Exploding {
        Exploding() {
            throw new IllegalStateException("boom");
        }
    }

    static final class NeedsExploding {
        @Inject
        NeedsExploding(Exploding exploding) {
        }
    }

    static final class GenericDependency {
        @Inject
        GenericDependency(List<String> values) {
        }
    }

    static final class TwoQualifiers {
        @Inject
        TwoQualifiers(@Named("a") @KeyTest.NotAQualifier String value) {
        }
    }

    static final class Target {
        @Inject
        Engine engine;

        @Inject
        Registry registry;
    }

    private ConstructorInjectionTest() {
    }

    public static void run() {
        Assertions.suite("Constructor and member injection");

        Assertions.test("just-in-time binding builds a concrete class", () -> {
            try (Container container = Container.create()) {
                Car car = container.getInstance(Car.class);
                Assertions.assertTrue("engine injected", car.engine != null);
            }
        });

        Assertions.test("unannotated classes are prototypes", () -> {
            try (Container container = Container.create()) {
                Assertions.assertTrue("distinct", container.getInstance(Engine.class) != container.getInstance(Engine.class));
            }
        });

        Assertions.test("@Singleton on the class caches one instance", () -> {
            try (Container container = Container.create()) {
                Assertions.assertTrue("same", container.getInstance(Registry.class) == container.getInstance(Registry.class));
            }
        });

        Assertions.test("more than one @Inject constructor is an error", () -> {
            try (Container container = Container.create()) {
                Assertions.assertThrows("rejected", ConfigurationException.class,
                    () -> container.getInstance(TwoInjectConstructors.class));
            }
        });

        Assertions.test("a class without @Inject or no-arg constructor is an error", () -> {
            try (Container container = Container.create()) {
                try {
                    container.getInstance(NoUsableConstructor.class);
                    throw new AssertionError("expected failure");
                } catch (ConfigurationException e) {
                    Assertions.assertTrue("message", e.getMessage().contains("needs an @Inject constructor"));
                }
            }
        });

        Assertions.test("interfaces, abstract, inner and JDK classes need explicit bindings", () -> {
            try (Container container = Container.create()) {
                Assertions.assertThrows("interface", ConfigurationException.class, () -> container.getInstance(Runnable.class));
                Assertions.assertThrows("abstract", ConfigurationException.class, () -> container.getInstance(AbstractThing.class));
                Assertions.assertThrows("inner", ConfigurationException.class, () -> container.getInstance(InnerClass.class));
                Assertions.assertThrows("jdk", ConfigurationException.class, () -> container.getInstance(StringBuilder.class));
            }
        });

        Assertions.test("members inject superclass first, fields before methods", () -> {
            try (Container container = Container.create()) {
                Derived derived = container.getInstance(Derived.class);
                Assertions.assertEquals("order", List.of(
                    "constructor",
                    "base-method engine=true field=true",
                    "derived-method registry=true"), derived.events);
                Assertions.assertTrue("base field", derived.baseEngine != null);
                Assertions.assertTrue("derived field", derived.registry != null);
            }
        });

        Assertions.test("an override without @Inject disables the inherited injection", () -> {
            try (Container container = Container.create()) {
                Derived derived = container.getInstance(Derived.class);
                Assertions.assertFalse("base not called", derived.events.contains("base-overridden"));
                Assertions.assertFalse("derived not called", derived.events.contains("derived-overridden"));
            }
        });

        Assertions.test("final fields cannot be injected", () -> {
            try (Container container = Container.create()) {
                Assertions.assertThrows("final", ConfigurationException.class, () -> container.getInstance(FinalField.class));
            }
        });

        Assertions.test("constructor exceptions are wrapped with context and cause", () -> {
            try (Container container = Container.create()) {
                try {
                    container.getInstance(NeedsExploding.class);
                    throw new AssertionError("expected failure");
                } catch (ProvisionException e) {
                    Assertions.assertTrue("context", e.getMessage().contains("Exploding constructor"));
                    Assertions.assertTrue("cause", e.getCause() instanceof IllegalStateException);
                }
            }
        });

        Assertions.test("parameterized dependencies are rejected", () -> {
            try (Container container = Container.create()) {
                Assertions.assertThrows("generic", ConfigurationException.class,
                    () -> container.getInstance(GenericDependency.class));
            }
        });

        Assertions.test("injectMembers fills an existing object", () -> {
            try (Container container = Container.create()) {
                Target target = container.injectMembers(new Target());
                Assertions.assertTrue("engine", target.engine != null);
                Assertions.assertTrue("singleton", target.registry == container.getInstance(Registry.class));
            }
        });

        Assertions.test("a non-qualifier annotation next to a qualifier is fine", () -> {
            try (Container container = Container.create(binder -> binder.bind(String.class).named("a").toInstance("x"))) {
                Assertions.assertTrue("built", container.getInstance(TwoQualifiers.class) != null);
            }
        });
    }
}
