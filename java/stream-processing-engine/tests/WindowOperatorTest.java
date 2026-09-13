import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.ArrayList;
import java.util.List;
import stream.Aggregators;
import stream.Event;
import stream.SessionWindows;
import stream.SlidingWindows;
import stream.TumblingWindows;
import stream.Window;
import stream.WindowAssigner;
import stream.WindowOperator;
import stream.WindowResult;

public final class WindowOperatorTest {

    private WindowOperatorTest() {
    }

    public static void run() {
        Assertions.suite("WindowOperator");

        Assertions.test("tumbling windows fire once the watermark passes their end", () -> {
            WindowOperator<Long, Long, Long> operator = counter(TumblingWindows.of(10), 0);
            List<Event<Long>> late = new ArrayList<>();
            operator.onElement(Event.of("a", 1L, 1), late::add);
            operator.onElement(Event.of("a", 1L, 8), late::add);
            operator.onElement(Event.of("b", 1L, 5), late::add);
            Assertions.assertEquals("not yet", List.of(), operator.onWatermark(8));
            List<WindowResult<Long>> fired = operator.onWatermark(9);
            Assertions.assertEquals("two keys", 2, fired.size());
            Assertions.assertEquals("a count", 2L, fired.get(0).value());
            Assertions.assertEquals("b count", 1L, fired.get(1).value());
            Assertions.assertFalse("on time", fired.get(0).late());
            Assertions.assertEquals("purged", 0, operator.openWindowCount());
            Assertions.assertTrue("no late events", late.isEmpty());
        });

        Assertions.test("stale watermarks are ignored", () -> {
            WindowOperator<Long, Long, Long> operator = counter(TumblingWindows.of(10), 0);
            operator.onElement(Event.of("a", 1L, 1), event -> { });
            operator.onWatermark(20);
            Assertions.assertEquals("regression ignored", List.of(), operator.onWatermark(15));
            Assertions.assertEquals("watermark kept", 20L, operator.currentWatermark());
        });

        Assertions.test("results come out ordered by window end then key", () -> {
            WindowOperator<Long, Long, Long> operator = counter(TumblingWindows.of(10), 0);
            operator.onElement(Event.of("z", 1L, 3), event -> { });
            operator.onElement(Event.of("a", 1L, 15), event -> { });
            operator.onElement(Event.of("m", 1L, 4), event -> { });
            List<WindowResult<Long>> fired = operator.onWatermark(100);
            Assertions.assertEquals("keys", List.of("m", "z", "a"),
                fired.stream().map(WindowResult::key).toList());
        });

        Assertions.test("sliding windows count an element in every overlapping window", () -> {
            WindowOperator<Long, Long, Long> operator = counter(SlidingWindows.of(10, 5), 0);
            operator.onElement(Event.of("k", 1L, 7), event -> { });
            operator.onElement(Event.of("k", 1L, 12), event -> { });
            List<WindowResult<Long>> fired = operator.onWatermark(100);
            Assertions.assertEquals("windows", List.of(new Window(0, 10), new Window(5, 15), new Window(10, 20)),
                fired.stream().map(WindowResult::window).toList());
            Assertions.assertEquals("counts", List.of(1L, 2L, 1L), fired.stream().map(WindowResult::value).toList());
        });

        Assertions.test("session windows merge when an element bridges two sessions", () -> {
            WindowOperator<Long, Long, Long> operator =
                new WindowOperator<>(SessionWindows.withGap(10), Aggregators.sum(Long::longValue), 0);
            operator.onElement(Event.of("u", 1L, 0), event -> { });
            operator.onElement(Event.of("u", 2L, 18), event -> { });
            Assertions.assertEquals("two sessions", List.of(new Window(0, 10), new Window(18, 28)),
                operator.openWindows("u"));
            operator.onElement(Event.of("u", 4L, 9), event -> { });
            Assertions.assertEquals("merged", List.of(new Window(0, 28)), operator.openWindows("u"));
            Assertions.assertEquals("not before end", List.of(), operator.onWatermark(26));
            List<WindowResult<Long>> fired = operator.onWatermark(27);
            Assertions.assertEquals("one result", 1, fired.size());
            Assertions.assertEquals("summed across merge", 7L, fired.get(0).value());
        });

        Assertions.test("sessions stay separate per key and across gaps", () -> {
            WindowOperator<Long, Long, Long> operator = counter(SessionWindows.withGap(5), 0);
            operator.onElement(Event.of("a", 1L, 0), event -> { });
            operator.onElement(Event.of("a", 1L, 3), event -> { });
            operator.onElement(Event.of("b", 1L, 4), event -> { });
            operator.onElement(Event.of("a", 1L, 20), event -> { });
            List<WindowResult<Long>> fired = operator.onWatermark(1000);
            Assertions.assertEquals("three sessions", 3, fired.size());
            Assertions.assertEquals("a first", new Window(0, 8), fired.get(0).window());
            Assertions.assertEquals("a first count", 2L, fired.get(0).value());
        });

        Assertions.test("elements behind the watermark beyond lateness go to the late output", () -> {
            WindowOperator<Long, Long, Long> operator = counter(TumblingWindows.of(10), 0);
            List<Event<Long>> late = new ArrayList<>();
            operator.onElement(Event.of("a", 1L, 5), late::add);
            operator.onWatermark(15);
            List<WindowResult<Long>> updates = operator.onElement(Event.of("a", 1L, 7), late::add);
            Assertions.assertEquals("no update", List.of(), updates);
            Assertions.assertEquals("late captured", 1, late.size());
            Assertions.assertEquals("dropped counter", 1L, operator.droppedCount());
        });

        Assertions.test("allowed lateness emits updated results and keeps state until cleanup", () -> {
            WindowOperator<Long, Long, Long> operator = counter(TumblingWindows.of(10), 5);
            List<Event<Long>> late = new ArrayList<>();
            operator.onElement(Event.of("a", 1L, 2), late::add);
            List<WindowResult<Long>> first = operator.onWatermark(10);
            Assertions.assertEquals("first firing", 1L, first.get(0).value());
            List<WindowResult<Long>> update = operator.onElement(Event.of("a", 1L, 4), late::add);
            Assertions.assertEquals("one update", 1, update.size());
            Assertions.assertEquals("updated count", 2L, update.get(0).value());
            Assertions.assertTrue("marked late", update.get(0).late());
            Assertions.assertEquals("second emission", 2, update.get(0).emission());
            Assertions.assertEquals("no refire on watermark", List.of(), operator.onWatermark(13));
            Assertions.assertEquals("still open", 1, operator.openWindowCount());
            operator.onWatermark(14);
            Assertions.assertEquals("cleaned up", 0, operator.openWindowCount());
            operator.onElement(Event.of("a", 1L, 6), late::add);
            Assertions.assertEquals("now dropped", 1, late.size());
        });

        Assertions.test("a late element within lateness can open a window that never fired", () -> {
            WindowOperator<Long, Long, Long> operator = counter(TumblingWindows.of(10), 20);
            operator.onElement(Event.of("a", 1L, 25), event -> { });
            operator.onWatermark(24);
            List<WindowResult<Long>> update = operator.onElement(Event.of("b", 1L, 3), event -> { });
            Assertions.assertEquals("emitted immediately", 1, update.size());
            Assertions.assertEquals("first emission", 1, update.get(0).emission());
            Assertions.assertTrue("late", update.get(0).late());
        });

        Assertions.test("sliding window partially late only drops expired panes", () -> {
            WindowOperator<Long, Long, Long> operator = counter(SlidingWindows.of(10, 5), 0);
            List<Event<Long>> late = new ArrayList<>();
            operator.onElement(Event.of("k", 1L, 20), late::add);
            operator.onWatermark(12);
            List<WindowResult<Long>> updates = operator.onElement(Event.of("k", 1L, 9), late::add);
            Assertions.assertEquals("no immediate update", List.of(), updates);
            Assertions.assertTrue("not late overall", late.isEmpty());
            Assertions.assertEquals("kept pane", List.of(new Window(5, 15), new Window(15, 25), new Window(20, 30)),
                operator.openWindows("k"));
        });

        Assertions.test("snapshot and restore reproduce identical future output", () -> {
            WindowOperator<Long, Long, Long> original = counter(SessionWindows.withGap(10), 3);
            original.onElement(Event.of("a", 1L, 1), event -> { });
            original.onElement(Event.of("b", 1L, 4), event -> { });
            original.onElement(Event.of("a", 1L, 8), event -> { });
            original.onWatermark(12);
            WindowOperator<Long, Long, Long> restored = counter(SessionWindows.withGap(10), 3);
            try {
                ByteArrayOutputStream buffer = new ByteArrayOutputStream();
                original.snapshot(new DataOutputStream(buffer));
                restored.restore(new DataInputStream(new ByteArrayInputStream(buffer.toByteArray())));
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
            Assertions.assertEquals("watermark", original.currentWatermark(), restored.currentWatermark());
            Assertions.assertEquals("windows", original.openWindows("a"), restored.openWindows("a"));
            List<WindowResult<Long>> expected = new ArrayList<>(original.onElement(Event.of("b", 1L, 5), event -> { }));
            expected.addAll(original.onWatermark(500));
            List<WindowResult<Long>> actual = new ArrayList<>(restored.onElement(Event.of("b", 1L, 5), event -> { }));
            actual.addAll(restored.onWatermark(500));
            Assertions.assertEquals("same results", expected, actual);
        });

        Assertions.test("negative lateness is rejected", () -> {
            Assertions.assertThrows("negative", IllegalArgumentException.class,
                () -> counter(TumblingWindows.of(1), -1));
        });
    }

    private static WindowOperator<Long, Long, Long> counter(WindowAssigner assigner, long lateness) {
        return new WindowOperator<>(assigner, Aggregators.count(), lateness);
    }
}
