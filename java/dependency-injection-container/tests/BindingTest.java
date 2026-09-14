import di.Binder;
import di.ConfigurationException;
import di.Container;
import di.Inject;
import di.Key;
import di.Module;
import di.Named;
import di.Provider;
import di.Provides;
import di.ProvisionException;
import di.Scope;
import di.Singleton;
import java.util.concurrent.atomic.AtomicInteger;

public final class BindingTest {

    interface Greeter {
        String greet(String name);
    }

    static final class EnglishGreeter implements Greeter {
        @Override
        public String greet(String name) {
            return "hello " + name;
        }
    }

    static final class ThaiGreeter implements Greeter {
        @Override
        public String greet(String name) {
            return "sawasdee " + name;
        }
    }

    static final class Server {
        final int port;
        final String host;
        final Greeter greeter;
        final Greeter fallback;

        @Inject
        Server(@Named("port") int port, @Named("host") String host, Greeter greeter, @Named("fallback") Greeter fallback) {
            this.port = port;
            this.host = host;
            this.greeter = greeter;
            this.fallback = fallback;
        }
    }

    static final class Counter {
        static final AtomicInteger CREATED = new AtomicInteger();

        Counter() {
            CREATED.incrementAndGet();
        }
    }

    static final class ConfigModule implements Module {
        @Override
        public void configure(Binder binder) {
            binder.bind(Greeter.class).to(EnglishGreeter.class);
            binder.bind(Greeter.class).named("fallback").to(ThaiGreeter.class);
        }

        @Provides
        @Named("port")
        int port() {
            return 8080;
        }

        @Provides
        @Named("host")
        String host(@Named("port") int port) {
            return "localhost:" + port;
        }

        @Provides
        @Singleton
        StringBuilder buffer() {
            return new StringBuilder("shared");
        }
    }

    static final class NullModule implements Module {
        @Override
        public void configure(Binder binder) {
        }

        @Provides
        @Named("nothing")
        String nothing() {
            return null;
        }
    }

    static final class NeedsContainer {
        final Container container;

        @Inject
        NeedsContainer(Container container) {
            this.container = container;
        }
    }

    private BindingTest() {
    }

    public static void run() {
        Assertions.suite("Bindings");

        Assertions.test("linked, qualified and @Provides bindings resolve together", () -> {
            try (Container container = Container.create(new ConfigModule())) {
                Server server = container.getInstance(Server.class);
                Assertions.assertEquals("port", 8080, server.port);
                Assertions.assertEquals("host uses port", "localhost:8080", server.host);
                Assertions.assertEquals("default greeter", "hello bob", server.greeter.greet("bob"));
                Assertions.assertEquals("named greeter", "sawasdee bob", server.fallback.greet("bob"));
            }
        });

        Assertions.test("@Singleton @Provides methods are cached", () -> {
            try (Container container = Container.create(new ConfigModule())) {
                Assertions.assertTrue("same", container.getInstance(StringBuilder.class) == container.getInstance(StringBuilder.class));
            }
        });

        Assertions.test("instance bindings return the exact object", () -> {
            Greeter greeter = new EnglishGreeter();
            try (Container container = Container.create(binder -> binder.bind(Greeter.class).toInstance(greeter))) {
                Assertions.assertTrue("same", container.getInstance(Greeter.class) == greeter);
            }
        });

        Assertions.test("instance bindings cannot be prototypes", () ->
            Assertions.assertThrows("rejected", ConfigurationException.class, () -> Container.create(binder ->
                binder.bind(Greeter.class).toInstance(new EnglishGreeter()).in(Scope.PROTOTYPE))));

        Assertions.test("provider bindings call the provider per request unless scoped", () -> {
            AtomicInteger calls = new AtomicInteger();
            Provider<Greeter> provider = () -> {
                calls.incrementAndGet();
                return new EnglishGreeter();
            };
            try (Container container = Container.create(
                binder -> binder.bind(Greeter.class).toProvider(provider),
                binder -> binder.bind(Greeter.class).named("cached").toProvider(provider).in(Scope.SINGLETON))) {
                container.getInstance(Greeter.class);
                container.getInstance(Greeter.class);
                container.getInstance(Key.named(Greeter.class, "cached"));
                container.getInstance(Key.named(Greeter.class, "cached"));
                Assertions.assertEquals("calls", 3, calls.get());
            }
        });

        Assertions.test("a provider returning null is a provision error", () -> {
            try (Container container = Container.create(binder -> binder.bind(Greeter.class).toProvider(() -> null))) {
                Assertions.assertThrows("null", ProvisionException.class, () -> container.getInstance(Greeter.class));
            }
        });

        Assertions.test("a @Provides method returning null is a provision error", () -> {
            try (Container container = Container.create(new NullModule())) {
                Assertions.assertThrows("null", ProvisionException.class,
                    () -> container.getInstance(Key.named(String.class, "nothing")));
            }
        });

        Assertions.test("duplicate bindings are reported", () -> {
            try {
                Container.create(
                    binder -> binder.bind(Greeter.class).to(EnglishGreeter.class),
                    binder -> binder.bind(Greeter.class).to(ThaiGreeter.class));
                throw new AssertionError("expected failure");
            } catch (ConfigurationException e) {
                Assertions.assertTrue("message", e.getMessage().startsWith("Duplicate binding for Greeter"));
            }
        });

        Assertions.test("binding to two targets is an error", () ->
            Assertions.assertThrows("two targets", ConfigurationException.class, () -> Container.create(binder ->
                binder.bind(Greeter.class).to(EnglishGreeter.class).toInstance(new ThaiGreeter()))));

        Assertions.test("all missing bindings are collected into one exception", () -> {
            try {
                Container.create(binder -> binder.bind(Server.class));
                throw new AssertionError("expected failure");
            } catch (ConfigurationException e) {
                Assertions.assertEquals("count", 4, e.messages().size());
                Assertions.assertTrue("site", e.messages().get(0).contains("required by parameter 0 of Server constructor"));
            }
        });

        Assertions.test("eager singletons are created at startup", () -> {
            int before = Counter.CREATED.get();
            try (Container container = Container.create(binder -> binder.bind(Counter.class).asEagerSingleton())) {
                Assertions.assertEquals("created at startup", before + 1, Counter.CREATED.get());
                container.getInstance(Counter.class);
                Assertions.assertEquals("not created again", before + 1, Counter.CREATED.get());
            }
        });

        Assertions.test("installing the same module twice is a no-op", () -> {
            Module module = binder -> binder.bind(Greeter.class).to(EnglishGreeter.class);
            try (Container container = Container.create(module, binder -> binder.install(module))) {
                Assertions.assertEquals("bound", "hello x", container.getInstance(Greeter.class).greet("x"));
            }
        });

        Assertions.test("the container injects itself", () -> {
            try (Container container = Container.create()) {
                Assertions.assertTrue("self", container.getInstance(NeedsContainer.class).container == container);
            }
        });

        Assertions.test("rebinding Container is rejected", () ->
            Assertions.assertThrows("self binding", ConfigurationException.class, () ->
                Container.create(binder -> binder.bind(Container.class).toProvider(() -> null))));

        Assertions.test("explain renders the dependency tree", () -> {
            try (Container container = Container.create(new ConfigModule())) {
                String tree = container.explain(Server.class);
                Assertions.assertTrue("root", tree.startsWith("Server [prototype, constructor Server]"));
                Assertions.assertTrue("linked", tree.contains("  Greeter [prototype, linked to EnglishGreeter]"));
                Assertions.assertTrue("provides", tree.contains("@Named(\"port\") Integer [prototype, @Provides ConfigModule.port()]"));
            }
        });

        Assertions.test("addError fails container creation", () ->
            Assertions.assertThrows("error", ConfigurationException.class, () ->
                Container.create(binder -> binder.addError("custom problem"))));
    }
}
