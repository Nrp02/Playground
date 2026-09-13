import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import stream.Aggregators;
import stream.Checkpoint;
import stream.CheckpointException;
import stream.CheckpointStore;
import stream.Event;
import stream.Pipeline;
import stream.RunReport;
import stream.SessionWindows;
import stream.SlidingWindows;
import stream.TumblingWindows;
import stream.WatermarkAligner;
import stream.WindowOperator;
import stream.WindowResult;

public final class Main {

    private record PageView(String page, long latencyMs) {
    }

    private Main() {
    }

    public static void main(String[] args) {
        List<Event<PageView>> clicks = clickstream();
        System.out.println("clickstream: " + clicks.size() + " page views across "
            + clicks.stream().map(Event::key).distinct().count() + " users");

        tumblingCounts(clicks);
        slidingLatency(clicks);
        userSessions(clicks);
        lateData();
        failureRecovery(clicks);
        corruptedCheckpoint();
        partitionAlignment();
    }

    private static void tumblingCounts(List<Event<PageView>> clicks) {
        section("tumbling 60s views per user (5s out-of-orderness)");
        RunReport<PageView, Long> report = Pipeline.from(clicks)
            .window(TumblingWindows.of(60_000))
            .maxOutOfOrderness(5_000)
            .aggregate(Aggregators.count())
            .run();
        report.results().stream().limit(6).forEach(result -> System.out.println("  " + result));
        System.out.println("  ... " + report.results().size() + " windows, " + report.lateEvents().size()
            + " late events dropped");
    }

    private static void slidingLatency(List<Event<PageView>> clicks) {
        section("sliding 2m/30s mean latency for /checkout");
        RunReport<PageView, Double> report = Pipeline.from(clicks)
            .filter(event -> event.value().page().equals("/checkout"))
            .window(SlidingWindows.of(120_000, 30_000))
            .maxOutOfOrderness(20_000)
            .aggregate(Aggregators.average(PageView::latencyMs))
            .run();
        report.results().stream().filter(result -> result.key().equals("user-1")).limit(5)
            .forEach(result -> System.out.printf("  %s %s -> %.1f ms%n", result.key(), result.window(),
                result.value()));
        System.out.println("  " + report.eventsProcessed() + " checkout views kept, " + report.eventsFiltered()
            + " filtered out");
    }

    private static void userSessions(List<Event<PageView>> clicks) {
        section("session windows (45s inactivity gap), distinct pages per session");
        RunReport<PageView, Integer> report = Pipeline.from(clicks)
            .window(SessionWindows.withGap(45_000))
            .maxOutOfOrderness(20_000)
            .allowedLateness(10_000)
            .aggregate(Aggregators.distinctCount(PageView::page))
            .run();
        report.results().stream().limit(6).forEach(result -> System.out.println("  " + result + " pages, "
            + (result.window().length() / 1000) + "s long"));
        System.out.println("  ... " + report.results().size() + " session results");
    }

    private static void lateData() {
        section("late data: allowed lateness 10 on 10-wide windows");
        WindowOperator<Long, Long, Long> operator = new WindowOperator<>(TumblingWindows.of(10),
            Aggregators.sum(Long::longValue), 10);
        List<Event<Long>> late = new ArrayList<>();
        List<Event<Long>> events = List.of(Event.of("sensor", 5L, 3), Event.of("sensor", 7L, 8),
            Event.of("sensor", 1L, 14));
        for (Event<Long> event : events) {
            operator.onElement(event, late::add);
        }
        operator.onWatermark(12).forEach(result -> System.out.println("  watermark 12:  " + result));
        emit("  late ts=6:     ", operator.onElement(Event.of("sensor", 100L, 6), late::add));
        operator.onWatermark(19).forEach(result -> System.out.println("  watermark 19:  " + result));
        operator.onWatermark(25).forEach(result -> System.out.println("  watermark 25:  " + result));
        emit("  late ts=2:     ", operator.onElement(Event.of("sensor", 999L, 2), late::add));
        System.out.println("  side output: " + late + ", open windows " + operator.openWindowCount());
    }

    private static void failureRecovery(List<Event<PageView>> clicks) {
        section("checkpointing + crash recovery (exactly-once sink)");
        Pipeline<PageView, Long, Long> clean = Pipeline.from(clicks)
            .window(SlidingWindows.of(60_000, 20_000)).maxOutOfOrderness(5_000).allowedLateness(15_000)
            .checkpointEvery(50)
            .aggregate(Aggregators.sum(PageView::latencyMs));
        Pipeline<PageView, Long, Long> crashing = Pipeline.from(clicks)
            .window(SlidingWindows.of(60_000, 20_000)).maxOutOfOrderness(5_000).allowedLateness(15_000)
            .checkpointEvery(50).failAt(73, 180, 181, clicks.size() - 1)
            .aggregate(Aggregators.sum(PageView::latencyMs));
        RunReport<PageView, Long> expected = clean.run();
        RunReport<PageView, Long> actual = crashing.run();
        CheckpointStore store = crashing.checkpointStore();
        long latest = store.ids().get(store.ids().size() - 1);
        System.out.println("  clean run:    " + expected.results().size() + " results, "
            + expected.checkpoints() + " checkpoints");
        System.out.println("  crashing run: " + actual.results().size() + " results, " + actual.recoveries()
            + " recoveries, " + actual.discardedOutputs() + " uncommitted outputs discarded");
        System.out.println("  latest checkpoint #" + latest + " at offset " + store.latest().sourceOffset()
            + ", " + store.sizeInBytes(latest) + " bytes");
        System.out.println("  outputs identical: " + expected.results().equals(actual.results()));
    }

    private static void corruptedCheckpoint() {
        section("checkpoint integrity");
        CheckpointStore store = new CheckpointStore(2);
        store.save(new Checkpoint(1, 500, new byte[] {1, 2, 3, 4, 5, 6, 7, 8}));
        System.out.println("  saved checkpoint 1, restores offset " + store.latest().sourceOffset());
        store.corrupt(1, 30);
        try {
            store.latest();
            System.out.println("  corruption went unnoticed");
        } catch (CheckpointException e) {
            System.out.println("  after bit flip: " + e.getMessage());
        }
    }

    private static void partitionAlignment() {
        section("watermark alignment across 3 source partitions");
        WatermarkAligner aligner = new WatermarkAligner(3);
        step(aligner, "p0 -> 1000", aligner.update(0, 1000));
        step(aligner, "p1 -> 800", aligner.update(1, 800));
        step(aligner, "p2 -> 1200", aligner.update(2, 1200));
        step(aligner, "p1 -> 1500", aligner.update(1, 1500));
        step(aligner, "p0 idle", aligner.markIdle(0));
    }

    private static void section(String title) {
        System.out.println();
        System.out.println("== " + title);
    }

    private static void step(WatermarkAligner aligner, String label, long watermark) {
        String shown = watermark == Long.MIN_VALUE ? "none" : Long.toString(watermark);
        System.out.printf("  %-12s combined watermark = %s%n", label, shown);
    }

    private static void emit(String prefix, List<WindowResult<Long>> results) {
        if (results.isEmpty()) {
            System.out.println(prefix + "(no update)");
        }
        results.forEach(result -> System.out.println(prefix + result));
    }

    private static List<Event<PageView>> clickstream() {
        Random random = new Random(2026);
        String[] pages = {"/", "/search", "/product", "/cart", "/checkout"};
        List<Event<PageView>> events = new ArrayList<>();
        for (int user = 0; user < 4; user++) {
            long clock = random.nextInt(30_000);
            for (int view = 0; view < 60; view++) {
                clock += random.nextInt(10) == 0 ? 60_000 + random.nextInt(60_000) : 2_000 + random.nextInt(18_000);
                PageView pageView = new PageView(pages[random.nextInt(pages.length)], 40 + random.nextInt(400));
                events.add(Event.of("user-" + user, pageView, clock));
            }
        }
        events.sort((left, right) -> Long.compare(left.timestamp() + jitter(left), right.timestamp() + jitter(right)));
        return events;
    }

    private static long jitter(Event<PageView> event) {
        return Math.floorMod(event.value().latencyMs() * 7919L + event.timestamp(), 25_000L) - 12_500L;
    }
}
