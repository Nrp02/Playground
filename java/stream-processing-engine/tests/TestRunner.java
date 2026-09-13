public final class TestRunner {

    private TestRunner() {
    }

    public static void main(String[] args) {
        WindowAssignerTest.run();
        WatermarkTest.run();
        AggregatorTest.run();
        WindowOperatorTest.run();
        CheckpointTest.run();
        PipelineTest.run();

        System.out.println();
        System.out.println(Assertions.passedCount() + " passed, "
            + Assertions.failures().size() + " failed");
        if (!Assertions.failures().isEmpty()) {
            Assertions.failures().forEach(failure -> System.out.println("  " + failure));
            System.exit(1);
        }
    }
}
