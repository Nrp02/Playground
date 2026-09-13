import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import stream.Aggregators;
import stream.Event;
import stream.Pipeline;
import stream.RunReport;
import stream.SessionWindows;
import stream.SlidingWindows;
import stream.TumblingWindows;

public final class PipelineTest {

    private PipelineTest() {
    }

    public static void run() {
        Assertions.suite("Pipeline");

        Assertions.test("filters run before windowing", () -> {
            List<Event<Long>> source = List.of(
                Event.of("a", 5L, 1), Event.of("a", -3L, 2), Event.of("a", 8L, 3), Event.of("b", -1L, 4));
            RunReport<Long, Long> report = Pipeline.from(source)
                .filter(event -> event.value() > 0)
                .window(TumblingWindows.of(10))
                .aggregate(Aggregators.sum(Long::longValue))
                .run();
            Assertions.assertEquals("one result", 1, report.results().size());
            Assertions.assertEquals("sum of positives", 13L, report.results().get(0).value());
            Assertions.assertEquals("accepted", 2L, report.eventsProcessed());
            Assertions.assertEquals("filtered", 2L, report.eventsFiltered());
        });

        Assertions.test("out-of-orderness bound keeps disordered events on time", () -> {
            List<Event<Long>> source = List.of(
                Event.of("k", 1L, 12), Event.of("k", 1L, 3), Event.of("k", 1L, 15), Event.of("k", 1L, 9));
            RunReport<Long, Long> strict = Pipeline.from(source)
                .window(TumblingWindows.of(10)).aggregate(Aggregators.count()).run();
            RunReport<Long, Long> tolerant = Pipeline.from(source)
                .window(TumblingWindows.of(10)).maxOutOfOrderness(10).aggregate(Aggregators.count()).run();
            Assertions.assertEquals("strict drops", 2, strict.lateEvents().size());
            Assertions.assertEquals("tolerant keeps", 0, tolerant.lateEvents().size());
            Assertions.assertEquals("tolerant first window", 2L, tolerant.results().get(0).value());
        });

        Assertions.test("recovery from a failure yields exactly-once output", () -> {
            List<Event<Long>> source = randomSource(400, 11);
            RunReport<Long, Double> clean = Pipeline.from(source).window(SlidingWindows.of(40, 20))
                .maxOutOfOrderness(15).allowedLateness(10).checkpointEvery(25)
                .aggregate(Aggregators.average(Long::longValue)).run();
            RunReport<Long, Double> failing = Pipeline.from(source).maxOutOfOrderness(15).allowedLateness(10)
                .window(SlidingWindows.of(40, 20)).checkpointEvery(25)
                .failAt(37, 210, 211, 399)
                .aggregate(Aggregators.average(Long::longValue)).run();
            Assertions.assertEquals("results", clean.results(), failing.results());
            Assertions.assertEquals("late events", clean.lateEvents(), failing.lateEvents());
            Assertions.assertEquals("recoveries", 4, failing.recoveries());
            Assertions.assertTrue("replay discarded output", failing.discardedOutputs() > 0);
            Assertions.assertTrue("non-trivial output", clean.results().size() > 10);
        });

        Assertions.test("session pipelines recover identically across many failure points", () -> {
            List<Event<Long>> source = randomSource(300, 5);
            RunReport<Long, Long> clean = sessions(source).aggregate(Aggregators.sum(Long::longValue)).run();
            for (long failure = 0; failure < source.size(); failure += 17) {
                RunReport<Long, Long> failing = sessions(source).failAt(failure)
                    .aggregate(Aggregators.sum(Long::longValue)).run();
                Assertions.assertEquals("failure at " + failure, clean.results(), failing.results());
                Assertions.assertEquals("late at " + failure, clean.lateEvents(), failing.lateEvents());
            }
        });

        Assertions.test("checkpoints are taken on the configured interval", () -> {
            List<Event<Long>> source = randomSource(100, 3);
            Pipeline<Long, Long, Long> pipeline = Pipeline.from(source).window(TumblingWindows.of(50))
                .checkpointEvery(30).retainCheckpoints(2).aggregate(Aggregators.count());
            RunReport<Long, Long> report = pipeline.run();
            Assertions.assertEquals("initial plus three", 4, report.checkpoints());
            Assertions.assertEquals("retained", List.of(2L, 3L), pipeline.checkpointStore().ids());
            Assertions.assertEquals("offset", 90L, pipeline.checkpointStore().latest().sourceOffset());
            RunReport<Long, Long> again = pipeline.run();
            Assertions.assertEquals("rerunnable", report.results(), again.results());
        });

        Assertions.test("every accepted event is either counted or reported late", () -> {
            List<Event<Long>> source = randomSource(500, 99);
            RunReport<Long, Long> report = Pipeline.from(source).window(TumblingWindows.of(25))
                .maxOutOfOrderness(5).checkpointEvery(40).failAt(100, 333)
                .aggregate(Aggregators.count()).run();
            long counted = report.results().stream().mapToLong(result -> result.value()).sum();
            Assertions.assertEquals("conservation", (long) source.size(), counted + report.lateEvents().size());
        });

        Assertions.test("missing window assigner is reported", () -> {
            Assertions.assertThrows("no window", NullPointerException.class,
                () -> Pipeline.from(List.<Event<Long>>of()).aggregate(Aggregators.count()));
        });
    }

    private static Pipeline.Builder<Long> sessions(List<Event<Long>> source) {
        return Pipeline.from(source).window(SessionWindows.withGap(12)).maxOutOfOrderness(8)
            .allowedLateness(6).checkpointEvery(20);
    }

    private static List<Event<Long>> randomSource(int size, long seed) {
        Random random = new Random(seed);
        String[] keys = {"alpha", "beta", "gamma"};
        List<Event<Long>> events = new ArrayList<>();
        long clock = 0;
        for (int i = 0; i < size; i++) {
            clock += random.nextInt(4);
            long timestamp = Math.max(0, clock - random.nextInt(30));
            events.add(Event.of(keys[random.nextInt(keys.length)], (long) random.nextInt(100), timestamp));
        }
        return events;
    }
}
