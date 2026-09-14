import di.ConfigurationException;
import di.Container;
import di.Inject;
import di.LifecycleException;
import di.PostConstruct;
import di.PreDestroy;
import di.Scope;
import di.Singleton;
import java.util.ArrayList;
import java.util.List;

public final class LifecycleTest {
    static final List<String> EVENTS = new ArrayList<>();

    @Singleton
    static final class Database {
        @PostConstruct
        void connect() {
            EVENTS.add("db up");
        }

        @PreDestroy
        void disconnect() {
            EVENTS.add("db down");
        }
    }

    @Singleton
    static final class Cache {
        @Inject
        Database database;

        @PostConstruct
        void warm() {
            EVENTS.add("cache up, database injected=" + (database != null));
        }

        @PreDestroy
        void drop() {
            EVENTS.add("cache down");
        }
    }

    static final class Session {
        @PreDestroy
        void end() {
            EVENTS.add("session down");
        }
    }

    static class BaseCallbacks {
        @PostConstruct
        void baseInit() {
            EVENTS.add("base init");
        }
    }

    static final class DerivedCallbacks extends BaseCallbacks {
        @PostConstruct
        void derivedInit() {
            EVENTS.add("derived init");
        }
    }

    @Singleton
    static final class FailingOne {
        @PreDestroy
        void stop() {
            throw new IllegalStateException("one");
        }
    }

    @Singleton
    static final class FailingTwo {
        @PreDestroy
        void stop() {
            throw new IllegalStateException("two");
        }
    }

    static final class BadCallback {
        @PostConstruct
        void init(String value) {
        }
    }

    interface Closer {
    }

    static final class ExternalCloser implements Closer {
        @PreDestroy
        void close() {
            EVENTS.add("external closed");
        }
    }

    static final class ManagedCloser implements Closer {
        @PreDestroy
        void close() {
            EVENTS.add("managed closed");
        }
    }

    private LifecycleTest() {
    }

    public static void run() {
        Assertions.suite("Lifecycle");

        Assertions.test("@PostConstruct runs after member injection", () -> {
            EVENTS.clear();
            try (Container container = Container.create()) {
                container.getInstance(Cache.class);
            }
            Assertions.assertEquals("start order", List.of("db up", "cache up, database injected=true"), EVENTS.subList(0, 2));
        });

        Assertions.test("@PreDestroy runs in reverse creation order on close", () -> {
            EVENTS.clear();
            Container container = Container.create();
            container.getInstance(Cache.class);
            container.close();
            Assertions.assertEquals("events", List.of("db up", "cache up, database injected=true", "cache down", "db down"), EVENTS);
        });

        Assertions.test("prototypes are not destroyed", () -> {
            EVENTS.clear();
            try (Container container = Container.create()) {
                container.getInstance(Session.class);
            }
            Assertions.assertEquals("nothing", List.of(), EVENTS);
        });

        Assertions.test("a prototype behind a singleton linked binding is destroyed", () -> {
            EVENTS.clear();
            try (Container container = Container.create(binder ->
                binder.bind(Closer.class).to(ManagedCloser.class).in(Scope.SINGLETON))) {
                container.getInstance(Closer.class);
            }
            Assertions.assertEquals("destroyed", List.of("managed closed"), EVENTS);
        });

        Assertions.test("user-supplied instances are not destroyed", () -> {
            EVENTS.clear();
            try (Container container = Container.create(
                binder -> binder.bind(ExternalCloser.class).toInstance(new ExternalCloser()),
                binder -> binder.bind(Closer.class).to(ExternalCloser.class).in(Scope.SINGLETON))) {
                container.getInstance(Closer.class);
            }
            Assertions.assertEquals("left alone", List.of(), EVENTS);
        });

        Assertions.test("superclass @PostConstruct runs first", () -> {
            EVENTS.clear();
            try (Container container = Container.create()) {
                container.getInstance(DerivedCallbacks.class);
            }
            Assertions.assertEquals("order", List.of("base init", "derived init"), EVENTS);
        });

        Assertions.test("close is idempotent and later lookups fail", () -> {
            Container container = Container.create();
            container.close();
            container.close();
            Assertions.assertTrue("closed", container.isClosed());
            Assertions.assertThrows("lookup", IllegalStateException.class, () -> container.getInstance(Database.class));
        });

        Assertions.test("every failing @PreDestroy runs and is reported", () -> {
            Container container = Container.create();
            container.getInstance(FailingOne.class);
            container.getInstance(FailingTwo.class);
            try {
                container.close();
                throw new AssertionError("expected failure");
            } catch (LifecycleException e) {
                Assertions.assertEquals("suppressed", 2, e.getSuppressed().length);
                Assertions.assertEquals("reverse order", "two", e.getSuppressed()[0].getCause().getMessage());
                Assertions.assertEquals("then first", "one", e.getSuppressed()[1].getCause().getMessage());
            }
        });

        Assertions.test("callbacks with parameters are configuration errors", () ->
            Assertions.assertThrows("params", ConfigurationException.class, () ->
                Container.create(binder -> binder.bind(BadCallback.class))));
    }
}
