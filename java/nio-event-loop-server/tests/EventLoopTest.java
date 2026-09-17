import java.time.Duration;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import nio.Cancellable;
import nio.EventLoop;

public final class EventLoopTest {

    private EventLoopTest() {
    }

    public static void run() {
        Assertions.suite("event loop");

        Assertions.test("tasks run in submission order on the loop thread", () -> {
            EventLoop loop = started();
            try {
                List<Integer> order = new CopyOnWriteArrayList<>();
                AtomicBoolean onLoop = new AtomicBoolean(true);
                CountDownLatch done = new CountDownLatch(1);
                for (int i = 0; i < 500; i++) {
                    int value = i;
                    loop.execute(() -> {
                        onLoop.compareAndSet(true, loop.inEventLoop());
                        order.add(value);
                    });
                }
                loop.execute(done::countDown);
                Assertions.assertTrue("drained", done.await(3, TimeUnit.SECONDS));
                Assertions.assertTrue("on loop", onLoop.get());
                Assertions.assertFalse("caller is not loop", loop.inEventLoop());
                for (int i = 0; i < 500; i++) {
                    Assertions.assertEquals("order " + i, i, order.get(i));
                }
            } finally {
                stop(loop);
            }
        });

        Assertions.test("timers fire in deadline order, ties by submission", () -> {
            EventLoop loop = started();
            try {
                List<String> fired = new CopyOnWriteArrayList<>();
                CountDownLatch done = new CountDownLatch(4);
                loop.schedule(() -> { fired.add("late"); done.countDown(); }, Duration.ofMillis(80));
                loop.schedule(() -> { fired.add("early-1"); done.countDown(); }, Duration.ofMillis(20));
                loop.schedule(() -> { fired.add("early-2"); done.countDown(); }, Duration.ofMillis(20));
                loop.schedule(() -> { fired.add("now"); done.countDown(); }, Duration.ZERO);
                Assertions.assertTrue("all fired", done.await(3, TimeUnit.SECONDS));
                Assertions.assertEquals("order", List.of("now", "early-1", "early-2", "late"), fired);
            } finally {
                stop(loop);
            }
        });

        Assertions.test("timers respect their delay", () -> {
            EventLoop loop = started();
            try {
                CountDownLatch done = new CountDownLatch(1);
                long start = System.nanoTime();
                long[] elapsed = new long[1];
                loop.schedule(() -> {
                    elapsed[0] = System.nanoTime() - start;
                    done.countDown();
                }, Duration.ofMillis(60));
                Assertions.assertTrue("fired", done.await(3, TimeUnit.SECONDS));
                Assertions.assertTrue("not early: " + elapsed[0], elapsed[0] >= TimeUnit.MILLISECONDS.toNanos(60));
            } finally {
                stop(loop);
            }
        });

        Assertions.test("cancelled timers never fire and fired timers cannot be cancelled", () -> {
            EventLoop loop = started();
            try {
                AtomicInteger count = new AtomicInteger();
                Cancellable cancelled = loop.schedule(count::incrementAndGet, Duration.ofMillis(30));
                Assertions.assertTrue("cancel succeeds", cancelled.cancel());
                Assertions.assertFalse("second cancel", cancelled.cancel());
                CountDownLatch fired = new CountDownLatch(1);
                Cancellable kept = loop.schedule(fired::countDown, Duration.ofMillis(5));
                Assertions.assertTrue("kept fired", fired.await(3, TimeUnit.SECONDS));
                Thread.sleep(80);
                Assertions.assertEquals("cancelled never ran", 0, count.get());
                Assertions.assertFalse("cannot cancel after firing", kept.cancel());
                Assertions.assertTrue("cancel flag", cancelled.isCancelled());
            } finally {
                stop(loop);
            }
        });

        Assertions.test("a throwing task does not kill the loop", () -> {
            EventLoop loop = started();
            try {
                loop.execute(() -> {
                    throw new IllegalStateException("boom");
                });
                CountDownLatch after = new CountDownLatch(1);
                loop.execute(after::countDown);
                Assertions.assertTrue("still running", after.await(3, TimeUnit.SECONDS));
                Assertions.assertEquals("failure counted", 1L, loop.taskFailures());
                Assertions.assertTrue("failure kept", loop.lastFailure() instanceof IllegalStateException);
            } finally {
                stop(loop);
            }
        });

        Assertions.test("shutdown drains queued tasks then rejects new ones", () -> {
            EventLoop loop = started();
            AtomicInteger ran = new AtomicInteger();
            CountDownLatch gate = new CountDownLatch(1);
            loop.execute(() -> {
                try {
                    gate.await();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            });
            for (int i = 0; i < 10; i++) {
                loop.execute(ran::incrementAndGet);
            }
            loop.shutdown();
            gate.countDown();
            Assertions.assertTrue("terminated", loop.awaitTermination(Duration.ofSeconds(3)));
            Assertions.assertEquals("queued tasks ran", 10, ran.get());
            Assertions.assertThrows("rejects", RejectedExecutionException.class, () -> loop.execute(ran::incrementAndGet));
        });

        Assertions.test("a loop cannot be started twice", () -> {
            EventLoop loop = started();
            try {
                Assertions.assertThrows("double start", IllegalStateException.class, loop::start);
            } finally {
                stop(loop);
            }
        });
    }

    private static EventLoop started() throws Exception {
        EventLoop loop = new EventLoop("test-loop");
        loop.start();
        return loop;
    }

    private static void stop(EventLoop loop) throws InterruptedException {
        loop.shutdown();
        if (!loop.awaitTermination(Duration.ofSeconds(3))) {
            throw new AssertionError("loop did not terminate");
        }
    }
}
