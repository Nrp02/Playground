import di.Container;
import di.Inject;
import di.Provider;
import di.Singleton;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.ArrayList;

public final class ConcurrencyTest {
    static final AtomicInteger CREATED = new AtomicInteger();

    @Singleton
    static final class SlowSingleton {
        SlowSingleton() {
            CREATED.incrementAndGet();
            try {
                Thread.sleep(20);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    static final class Consumer {
        final Provider<SlowSingleton> singleton;

        @Inject
        Consumer(Provider<SlowSingleton> singleton) {
            this.singleton = singleton;
        }
    }

    private ConcurrencyTest() {
    }

    public static void run() {
        Assertions.suite("Concurrency");

        Assertions.test("a singleton is created exactly once under contention", () -> {
            CREATED.set(0);
            int threads = 16;
            ExecutorService pool = Executors.newFixedThreadPool(threads);
            try (Container container = Container.create()) {
                CountDownLatch start = new CountDownLatch(1);
                List<Future<SlowSingleton>> futures = new ArrayList<>();
                for (int i = 0; i < threads; i++) {
                    futures.add(pool.submit(() -> {
                        start.await();
                        return container.getInstance(Consumer.class).singleton.get();
                    }));
                }
                start.countDown();
                Set<SlowSingleton> distinct = Collections.newSetFromMap(new IdentityHashMap<>());
                for (Future<SlowSingleton> future : futures) {
                    distinct.add(future.get(10, TimeUnit.SECONDS));
                }
                Assertions.assertEquals("one instance", 1, distinct.size());
                Assertions.assertEquals("one construction", 1, CREATED.get());
            } catch (Exception e) {
                throw new AssertionError(e);
            } finally {
                pool.shutdownNow();
            }
        });

        Assertions.test("resolution stacks are per thread", () -> {
            ExecutorService pool = Executors.newFixedThreadPool(4);
            try (Container container = Container.create()) {
                List<Future<Consumer>> futures = new ArrayList<>();
                for (int i = 0; i < 200; i++) {
                    futures.add(pool.submit(() -> container.getInstance(Consumer.class)));
                }
                for (Future<Consumer> future : futures) {
                    Assertions.assertTrue("resolved", future.get(10, TimeUnit.SECONDS) != null);
                }
            } catch (Exception e) {
                throw new AssertionError(e);
            } finally {
                pool.shutdownNow();
            }
        });
    }
}
