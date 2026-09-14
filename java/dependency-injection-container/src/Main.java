import di.Binder;
import di.ConfigurationException;
import di.Container;
import di.Inject;
import di.Key;
import di.LifecycleException;
import di.Matchers;
import di.Module;
import di.Named;
import di.PostConstruct;
import di.PreDestroy;
import di.Provider;
import di.Provides;
import di.Scope;
import di.Singleton;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class Main {

    @Retention(RetentionPolicy.RUNTIME)
    @Target(ElementType.METHOD)
    @interface Timed {
    }

    interface PaymentGateway {
        String charge(String customer, long cents);
    }

    interface OrderRepository {
        void save(String order);

        List<String> all();
    }

    @Singleton
    static final class AuditLog {
        private final List<String> entries = new ArrayList<>();

        @PostConstruct
        void open() {
            System.out.println("  [lifecycle] AuditLog opened");
        }

        void record(String entry) {
            entries.add(entry);
        }

        int size() {
            return entries.size();
        }

        @PreDestroy
        void flush() {
            System.out.println("  [lifecycle] AuditLog flushed " + entries.size() + " entries");
        }
    }

    static final class StripeGateway implements PaymentGateway {
        private final String apiKey;
        private final AuditLog audit;

        @Inject
        StripeGateway(@Named("stripe.key") String apiKey, AuditLog audit) {
            this.apiKey = apiKey;
            this.audit = audit;
        }

        @Timed
        @Override
        public String charge(String customer, long cents) {
            audit.record("charge " + customer + " " + cents);
            return "ch_" + apiKey.substring(0, 4) + "_" + customer + "_" + cents;
        }

        @PreDestroy
        void disconnect() {
            System.out.println("  [lifecycle] StripeGateway disconnected");
        }
    }

    @Singleton
    static final class InMemoryOrderRepository implements OrderRepository {
        private final List<String> orders = new ArrayList<>();

        @Override
        public void save(String order) {
            orders.add(order);
        }

        @Override
        public List<String> all() {
            return List.copyOf(orders);
        }
    }

    static final class ReceiptPrinter {
        private static int created;
        private final int serial = ++created;

        String print(String order) {
            return "receipt #" + serial + " for " + order;
        }
    }

    static final class OrderService {
        private final PaymentGateway gateway;
        private final OrderRepository repository;
        private final Provider<ReceiptPrinter> printers;

        @Inject
        private AuditLog audit;

        @Inject
        OrderService(PaymentGateway gateway, OrderRepository repository, Provider<ReceiptPrinter> printers) {
            this.gateway = gateway;
            this.repository = repository;
            this.printers = printers;
        }

        String place(String customer, long cents) {
            String charge = gateway.charge(customer, cents);
            repository.save(charge);
            audit.record("order " + charge);
            return printers.get().print(charge);
        }
    }

    static final class RequestContext {
        private final String requestId;

        RequestContext(String requestId) {
            this.requestId = requestId;
        }

        String requestId() {
            return requestId;
        }
    }

    static final class CheckoutHandler {
        private final RequestContext context;
        private final OrderService orders;

        @Inject
        CheckoutHandler(RequestContext context, OrderService orders) {
            this.context = context;
            this.orders = orders;
        }

        String handle(String customer) {
            return context.requestId() + " -> " + orders.place(customer, 1999);
        }
    }

    static final class ShopModule implements Module {
        private final List<String> timings;

        ShopModule(List<String> timings) {
            this.timings = timings;
        }

        @Override
        public void configure(Binder binder) {
            binder.bind(PaymentGateway.class).to(StripeGateway.class).in(Scope.SINGLETON);
            binder.bind(OrderRepository.class).to(InMemoryOrderRepository.class);
            binder.bind(AuditLog.class).asEagerSingleton();
            binder.bindInterceptor(Matchers.any(), Matchers.annotatedWith(Timed.class), invocation -> {
                long start = System.nanoTime();
                try {
                    return invocation.proceed();
                } finally {
                    timings.add(invocation + " took " + (System.nanoTime() - start) / 1000 + "us");
                }
            });
        }

        @Provides
        @Named("stripe.key")
        String stripeKey() {
            return "sk_test_51Hx";
        }

        @Provides
        @Named("currency")
        String currency() {
            return "THB";
        }
    }

    static final class Chicken {
        @Inject
        Chicken(Egg egg) {
        }
    }

    static final class Egg {
        @Inject
        Egg(Chicken chicken) {
        }
    }

    static final class BrokenService {
        @Inject
        BrokenService(@Named("missing") String value, Runnable task) {
        }
    }

    private Main() {
    }

    public static void main(String[] args) {
        List<String> timings = new ArrayList<>();

        System.out.println("== building the container (AuditLog is eager)");
        Container container = Container.create(new ShopModule(timings));

        System.out.println();
        System.out.println("== dependency graph for OrderService");
        System.out.print(container.explain(OrderService.class));

        System.out.println();
        System.out.println("== placing orders");
        OrderService service = container.getInstance(OrderService.class);
        System.out.println("  " + service.place("alice", 4200));
        System.out.println("  " + service.place("bob", 1500));
        System.out.println("  gateway is a proxy: " + container.getInstance(PaymentGateway.class).getClass().getSimpleName()
            .startsWith("$Proxy"));
        System.out.println("  same gateway twice: "
            + (container.getInstance(PaymentGateway.class) == container.getInstance(PaymentGateway.class)));
        System.out.println("  new OrderService each time: "
            + (container.getInstance(OrderService.class) != container.getInstance(OrderService.class)));
        System.out.println("  currency: " + container.getInstance(Key.named(String.class, "currency")));
        timings.forEach(line -> System.out.println("  [timed] " + line.replaceAll("\\d+us", "<n>us")));

        System.out.println();
        System.out.println("== per-request child containers");
        for (String requestId : List.of("req-1", "req-2")) {
            try (Container request = container.createChild(binder ->
                binder.bind(RequestContext.class).toInstance(new RequestContext(requestId)))) {
                System.out.println("  " + request.getInstance(CheckoutHandler.class).handle("carol"));
            }
        }
        OrderRepository repository = container.getInstance(OrderRepository.class);
        System.out.println("  orders stored in shared repository: " + repository.all().size());
        System.out.println("  audit entries: " + container.getInstance(AuditLog.class).size());

        System.out.println();
        System.out.println("== configuration errors are collected up front");
        try {
            Container.create(binder -> binder.bind(Chicken.class));
        } catch (ConfigurationException e) {
            System.out.println("  " + e.getMessage());
        }
        try {
            Container.create(binder -> binder.bind(BrokenService.class));
        } catch (ConfigurationException e) {
            System.out.println("  " + e.getMessage().replace(System.lineSeparator(), System.lineSeparator() + "  "));
        }

        System.out.println();
        System.out.println("== closing (PreDestroy runs in reverse creation order)");
        try {
            container.close();
        } catch (LifecycleException e) {
            System.out.println("  unexpected: " + e.getMessage());
        }
        try {
            container.getInstance(OrderService.class);
        } catch (IllegalStateException e) {
            System.out.println("  after close: " + e.getMessage().toLowerCase(Locale.ROOT));
        }
    }
}
