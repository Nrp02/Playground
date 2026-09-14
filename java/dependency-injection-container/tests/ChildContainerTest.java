import di.ConfigurationException;
import di.Container;
import di.Inject;
import di.Key;
import di.PreDestroy;
import di.Singleton;
import java.util.ArrayList;
import java.util.List;

public final class ChildContainerTest {
    static final List<String> EVENTS = new ArrayList<>();

    @Singleton
    static final class AppConfig {
    }

    static final class Request {
        final String id;

        Request(String id) {
            this.id = id;
        }
    }

    static final class Handler {
        final Request request;
        final AppConfig config;

        @Inject
        Handler(Request request, AppConfig config) {
            this.request = request;
            this.config = config;
        }
    }

    @Singleton
    static final class RequestScoped {
        @PreDestroy
        void end() {
            EVENTS.add("request scoped closed");
        }
    }

    private ChildContainerTest() {
    }

    public static void run() {
        Assertions.suite("Child containers");

        Assertions.test("children see parent bindings and share parent singletons", () -> {
            try (Container parent = Container.create(binder -> binder.bind(AppConfig.class))) {
                AppConfig config = parent.getInstance(AppConfig.class);
                try (Container child = parent.createChild(binder -> binder.bind(Request.class).toInstance(new Request("r1")))) {
                    Handler handler = child.getInstance(Handler.class);
                    Assertions.assertEquals("request", "r1", handler.request.id);
                    Assertions.assertTrue("shared config", handler.config == config);
                    Assertions.assertTrue("parent link", child.parent() == parent);
                }
            }
        });

        Assertions.test("the parent cannot see child bindings", () -> {
            try (Container parent = Container.create()) {
                try (Container child = parent.createChild(binder -> binder.bind(Request.class).toInstance(new Request("r")))) {
                    Assertions.assertTrue("child has it", child.hasExplicitBinding(Key.of(Request.class)));
                    Assertions.assertFalse("parent does not", parent.hasExplicitBinding(Key.of(Request.class)));
                    Assertions.assertThrows("parent lookup", ConfigurationException.class, () -> parent.getInstance(Handler.class));
                }
            }
        });

        Assertions.test("sibling children get their own just-in-time singletons", () -> {
            try (Container parent = Container.create()) {
                Container first = parent.createChild();
                Container second = parent.createChild();
                Assertions.assertTrue("isolated", first.getInstance(RequestScoped.class) != second.getInstance(RequestScoped.class));
                Assertions.assertTrue("stable", first.getInstance(RequestScoped.class) == first.getInstance(RequestScoped.class));
            }
        });

        Assertions.test("children cannot override parent bindings", () -> {
            try (Container parent = Container.create(binder -> binder.bind(AppConfig.class))) {
                Assertions.assertThrows("override", ConfigurationException.class,
                    () -> parent.createChild(binder -> binder.bind(AppConfig.class)));
            }
        });

        Assertions.test("child validation uses parent bindings", () -> {
            try (Container parent = Container.create(binder -> binder.bind(Request.class).toInstance(new Request("p")))) {
                try (Container child = parent.createChild(binder -> binder.bind(Handler.class))) {
                    Assertions.assertEquals("resolved", "p", child.getInstance(Handler.class).request.id);
                }
            }
        });

        Assertions.test("closing the parent closes open children first", () -> {
            EVENTS.clear();
            Container parent = Container.create();
            Container child = parent.createChild();
            child.getInstance(RequestScoped.class);
            parent.close();
            Assertions.assertTrue("child closed", child.isClosed());
            Assertions.assertEquals("destroyed", List.of("request scoped closed"), EVENTS);
            Assertions.assertThrows("no new children", IllegalStateException.class, () -> parent.createChild());
        });

        Assertions.test("a child injects itself as the container", () -> {
            try (Container parent = Container.create(); Container child = parent.createChild()) {
                Assertions.assertTrue("child", child.getInstance(Container.class) == child);
            }
        });
    }
}
