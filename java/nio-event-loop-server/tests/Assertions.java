import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.function.BooleanSupplier;

public final class Assertions {
    private static int passed;
    private static final List<String> FAILURES = new ArrayList<>();
    private static String suite = "";

    @FunctionalInterface
    public interface Body {
        void run() throws Exception;
    }

    @FunctionalInterface
    public interface ThrowingBody {
        void run() throws Exception;
    }

    private Assertions() {
    }

    public static void suite(String name) {
        suite = name;
        System.out.println("-- " + name);
    }

    public static void test(String name, Body body) {
        try {
            body.run();
            passed++;
            System.out.println("   ok   " + name);
        } catch (AssertionError | Exception e) {
            FAILURES.add(suite + " / " + name + ": " + e);
            System.out.println("   FAIL " + name + " -> " + e);
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

    public static <T extends Throwable> T assertThrows(String message, Class<T> expected, ThrowingBody body) {
        try {
            body.run();
        } catch (Throwable thrown) {
            if (expected.isInstance(thrown)) {
                return expected.cast(thrown);
            }
            throw new AssertionError(message + " (threw " + thrown + ")");
        }
        throw new AssertionError(message + " (nothing was thrown)");
    }

    public static void eventually(String message, BooleanSupplier condition) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return;
            }
            Thread.sleep(5);
        }
        throw new AssertionError(message + " (condition not met within 5s)");
    }

    public static int passedCount() {
        return passed;
    }

    public static List<String> failures() {
        return List.copyOf(FAILURES);
    }
}
