public final class TestRunner {

    private TestRunner() {
    }

    public static void main(String[] args) {
        MessagingTest.run();
        LifecycleTest.run();
        SupervisionTest.run();
        BehaviorTest.run();
        BackpressureTest.run();
        RouterSchedulerTest.run();
        ConcurrencyTest.run();

        System.out.println();
        System.out.println(Assertions.passedCount() + " passed, "
            + Assertions.failures().size() + " failed");
        if (!Assertions.failures().isEmpty()) {
            Assertions.failures().forEach(failure -> System.out.println("  " + failure));
            System.exit(1);
        }
    }
}
