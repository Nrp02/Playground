public final class TestRunner {

    private TestRunner() {
    }

    public static void main(String[] args) {
        KeyTest.run();
        ConstructorInjectionTest.run();
        BindingTest.run();
        CycleTest.run();
        LifecycleTest.run();
        InterceptionTest.run();
        ChildContainerTest.run();
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
