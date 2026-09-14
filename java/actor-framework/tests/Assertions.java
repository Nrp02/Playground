import actor.ActorSystem;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.function.BooleanSupplier;

public final class Assertions {
    private static int passed;
    private static final List<String> FAILURES = new ArrayList<>();
    private static String suite = "";

    @FunctionalInterface
    public interface Body {
        void run(ActorSystem system) throws Exception;
    }

    private Assertions() {
    }

    public static void suite(String name) {
        suite = name;
        System.out.println("-- " + name);
    }

    public static void test(String name, Body body) {
        ActorSystem system = ActorSystem.create("test", 4, 16);
        try {
            body.run(system);
            passed++;
            System.out.println("   ok   " + name);
        } catch (AssertionError | Exception e) {
            FAILURES.add(suite + " / " + name + ": " + e);
            System.out.println("   FAIL " + name + " -> " + e);
        } finally {
            try {
                if (!system.shutdown(Duration.ofSeconds(5))) {
                    FAILURES.add(suite + " / " + name + ": actor system did not shut down gracefully");
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    public static void assertTrue(String message, boolean condition) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    public static void assertFalse(String message, boolean condition) {
        assertTrue(message, !condition);
    }

    public static void assertEquals(String message, Object expected, Object actual) {
        if (expected == null ? actual != null : !expected.equals(actual)) {
            throw new AssertionError(message + " (expected <" + expected + "> but was <" + actual + ">)");
        }
    }

    public static void assertThrows(String message, Class<? extends Throwable> expected, Runnable body) {
        try {
            body.run();
        } catch (Throwable thrown) {
            if (expected.isInstance(thrown)) {
                return;
            }
            throw new AssertionError(message + " (threw " + thrown.getClass().getName() + ")");
        }
        throw new AssertionError(message + " (nothing was thrown)");
    }

    public static <T> T await(CompletableFuture<T> future) throws Exception {
        return future.get(3, TimeUnit.SECONDS);
    }

    public static Throwable awaitFailure(CompletableFuture<?> future) throws Exception {
        try {
            future.get(3, TimeUnit.SECONDS);
        } catch (ExecutionException e) {
            return e.getCause();
        }
        throw new AssertionError("future completed normally");
    }

    public static void eventually(String message, BooleanSupplier condition) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return;
            }
            Thread.sleep(5);
        }
        throw new AssertionError(message + " (condition not met within 3s)");
    }

    public static int passedCount() {
        return passed;
    }

    public static List<String> failures() {
        return List.copyOf(FAILURES);
    }
}
