import java.util.List;
import stream.SessionWindows;
import stream.SlidingWindows;
import stream.TumblingWindows;
import stream.Window;

public final class WindowAssignerTest {

    private WindowAssignerTest() {
    }

    public static void run() {
        Assertions.suite("WindowAssigner");

        Assertions.test("tumbling aligns to size boundaries", () -> {
            TumblingWindows tumbling = TumblingWindows.of(10);
            Assertions.assertEquals("start of window", List.of(new Window(0, 10)), tumbling.assign(0));
            Assertions.assertEquals("inside window", List.of(new Window(0, 10)), tumbling.assign(9));
            Assertions.assertEquals("next window", List.of(new Window(10, 20)), tumbling.assign(10));
        });

        Assertions.test("tumbling handles negative timestamps and offsets", () -> {
            Assertions.assertEquals("negative", List.of(new Window(-10, 0)), TumblingWindows.of(10).assign(-1));
            Assertions.assertEquals("offset", List.of(new Window(3, 13)), TumblingWindows.of(10, 3).assign(12));
            Assertions.assertEquals("offset before", List.of(new Window(-7, 3)), TumblingWindows.of(10, 3).assign(2));
        });

        Assertions.test("sliding assigns every overlapping window in order", () -> {
            List<Window> windows = SlidingWindows.of(10, 5).assign(12);
            Assertions.assertEquals("two windows", List.of(new Window(5, 15), new Window(10, 20)), windows);
            List<Window> dense = SlidingWindows.of(9, 3).assign(7);
            Assertions.assertEquals("three windows",
                List.of(new Window(0, 9), new Window(3, 12), new Window(6, 15)), dense);
            dense.forEach(window -> Assertions.assertTrue("contains " + window, window.contains(7)));
        });

        Assertions.test("sliding with slide equal to size degenerates to tumbling", () -> {
            Assertions.assertEquals("same as tumbling", TumblingWindows.of(4).assign(6),
                SlidingWindows.of(4, 4).assign(6));
        });

        Assertions.test("session windows are proto-windows of gap length", () -> {
            SessionWindows sessions = SessionWindows.withGap(30);
            Assertions.assertEquals("proto", List.of(new Window(100, 130)), sessions.assign(100));
            Assertions.assertTrue("merging", sessions.isMerging());
            Assertions.assertFalse("tumbling not merging", TumblingWindows.of(5).isMerging());
        });

        Assertions.test("window geometry", () -> {
            Window window = new Window(5, 15);
            Assertions.assertEquals("max timestamp", 14L, window.maxTimestamp());
            Assertions.assertTrue("touching intersects", window.intersects(new Window(15, 20)));
            Assertions.assertFalse("apart", window.intersects(new Window(16, 20)));
            Assertions.assertEquals("cover", new Window(0, 20), window.cover(new Window(0, 20)));
            Assertions.assertTrue("ordering", new Window(1, 5).compareTo(new Window(1, 6)) < 0);
        });

        Assertions.test("invalid configurations are rejected", () -> {
            Assertions.assertThrows("empty window", IllegalArgumentException.class, () -> new Window(5, 5));
            Assertions.assertThrows("zero size", IllegalArgumentException.class, () -> TumblingWindows.of(0));
            Assertions.assertThrows("gaps", IllegalArgumentException.class, () -> SlidingWindows.of(5, 6));
            Assertions.assertThrows("gap", IllegalArgumentException.class, () -> SessionWindows.withGap(0));
        });
    }
}
