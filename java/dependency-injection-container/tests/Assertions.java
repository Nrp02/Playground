import java.util.ArrayList;
import java.util.List;

public final class Assertions {
    private static int passed;
    private static final List<String> FAILURES = new ArrayList<>();
    private static String suite = "";

    private Assertions() {
    }

    public static void suite(String name) {
        suite = name;
        System.out.println("-- " + name);
    }

    public static void test(String name, Runnable body) {
        try {
            body.run();
            passed++;
            System.out.println("   ok   " + name);
        } catch (AssertionError | RuntimeException e) {
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

    public static int passedCount() {
        return passed;
    }

    public static List<String> failures() {
        return List.copyOf(FAILURES);
    }
}
