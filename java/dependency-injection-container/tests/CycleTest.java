import di.CircularDependencyException;
import di.ConfigurationException;
import di.Container;
import di.Inject;
import di.Provider;
import di.Singleton;
import java.util.List;

public final class CycleTest {

    static final class A {
        @Inject
        A(B b) {
        }
    }

    static final class B {
        @Inject
        B(C c) {
        }
    }

    static final class C {
        @Inject
        C(A a) {
        }
    }

    static final class SelfField {
        @Inject
        SelfField self;
    }

    @Singleton
    static final class Parent {
        final Provider<Child> child;

        @Inject
        Parent(Provider<Child> child) {
            this.child = child;
        }
    }

    static final class Child {
        final Parent parent;

        @Inject
        Child(Parent parent) {
            this.parent = parent;
        }
    }

    static final class EagerLoop {
        @Inject
        EagerLoop(Provider<EagerLoop> self) {
            self.get();
        }
    }

    interface Service {
    }

    static final class ServiceImpl implements Service {
        @Inject
        ServiceImpl(Service service) {
        }
    }

    private CycleTest() {
    }

    public static void run() {
        Assertions.suite("Circular dependencies");

        Assertions.test("a constructor cycle is rejected at creation with its path", () -> {
            try {
                Container.create(binder -> binder.bind(A.class));
                throw new AssertionError("expected failure");
            } catch (ConfigurationException e) {
                Assertions.assertEquals("single error", 1, e.messages().size());
                Assertions.assertTrue("path", e.getMessage().startsWith("Circular dependency: A -> B -> C -> A"));
            }
        });

        Assertions.test("a field that injects its own type is a cycle", () -> {
            try {
                Container.create(binder -> binder.bind(SelfField.class));
                throw new AssertionError("expected failure");
            } catch (ConfigurationException e) {
                Assertions.assertTrue("path", e.getMessage().contains("SelfField -> SelfField"));
            }
        });

        Assertions.test("a linked binding back to its interface is a cycle", () -> {
            try {
                Container.create(binder -> binder.bind(Service.class).to(ServiceImpl.class));
                throw new AssertionError("expected failure");
            } catch (ConfigurationException e) {
                Assertions.assertTrue("path", e.getMessage().contains("Service -> ServiceImpl -> Service"));
            }
        });

        Assertions.test("just-in-time cycles are caught at runtime", () -> {
            try (Container container = Container.create()) {
                try {
                    container.getInstance(A.class);
                    throw new AssertionError("expected failure");
                } catch (CircularDependencyException e) {
                    Assertions.assertEquals("path", List.of("A", "B", "C", "A"), e.path());
                }
            }
        });

        Assertions.test("a Provider breaks the cycle", () -> {
            try (Container container = Container.create(binder -> binder.bind(Parent.class))) {
                Parent parent = container.getInstance(Parent.class);
                Child child = parent.child.get();
                Assertions.assertTrue("child sees the singleton parent", child.parent == parent);
            }
        });

        Assertions.test("calling a Provider for yourself during construction is detected", () -> {
            try (Container container = Container.create(binder -> binder.bind(EagerLoop.class))) {
                try {
                    container.getInstance(EagerLoop.class);
                    throw new AssertionError("expected failure");
                } catch (CircularDependencyException e) {
                    Assertions.assertEquals("path", List.of("EagerLoop", "EagerLoop"), e.path());
                }
            }
        });

        Assertions.test("the resolution stack is clean after a failure", () -> {
            try (Container container = Container.create()) {
                Assertions.assertThrows("cycle", CircularDependencyException.class, () -> container.getInstance(A.class));
                Assertions.assertTrue("unrelated resolve works", container.getInstance(Parent.class) != null);
            }
        });
    }
}
