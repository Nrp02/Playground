import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import stream.BoundedOutOfOrdernessWatermarks;
import stream.WatermarkAligner;

public final class WatermarkTest {

    private WatermarkTest() {
    }

    public static void run() {
        Assertions.suite("Watermarks");

        Assertions.test("no watermark before the first event", () -> {
            BoundedOutOfOrdernessWatermarks watermarks = new BoundedOutOfOrdernessWatermarks(5);
            Assertions.assertEquals("min", Long.MIN_VALUE, watermarks.currentWatermark());
        });

        Assertions.test("watermark trails max timestamp by the bound and never regresses", () -> {
            BoundedOutOfOrdernessWatermarks watermarks = new BoundedOutOfOrdernessWatermarks(5);
            watermarks.onEvent(100);
            Assertions.assertEquals("first", 94L, watermarks.currentWatermark());
            watermarks.onEvent(90);
            Assertions.assertEquals("out of order keeps", 94L, watermarks.currentWatermark());
            watermarks.onEvent(120);
            Assertions.assertEquals("advances", 114L, watermarks.currentWatermark());
        });

        Assertions.test("generator state survives snapshot and restore", () -> {
            BoundedOutOfOrdernessWatermarks original = new BoundedOutOfOrdernessWatermarks(2);
            original.onEvent(40);
            BoundedOutOfOrdernessWatermarks copy = new BoundedOutOfOrdernessWatermarks(2);
            try {
                ByteArrayOutputStream buffer = new ByteArrayOutputStream();
                original.snapshot(new DataOutputStream(buffer));
                copy.restore(new DataInputStream(new ByteArrayInputStream(buffer.toByteArray())));
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
            Assertions.assertEquals("same watermark", original.currentWatermark(), copy.currentWatermark());
        });

        Assertions.test("aligner holds back to the slowest partition", () -> {
            WatermarkAligner aligner = new WatermarkAligner(3);
            Assertions.assertEquals("one partition only", Long.MIN_VALUE, aligner.update(0, 50));
            aligner.update(1, 30);
            Assertions.assertEquals("still waiting on 2", Long.MIN_VALUE, aligner.update(0, 60));
            Assertions.assertEquals("min of all", 30L, aligner.update(2, 70));
            Assertions.assertEquals("slowest moves", 60L, aligner.update(1, 90));
        });

        Assertions.test("idle partitions stop holding the watermark back", () -> {
            WatermarkAligner aligner = new WatermarkAligner(2);
            aligner.update(0, 100);
            Assertions.assertEquals("blocked", Long.MIN_VALUE, aligner.current());
            Assertions.assertEquals("idle releases", 100L, aligner.markIdle(1));
            Assertions.assertTrue("idle flag", aligner.isIdle(1));
            Assertions.assertEquals("rejoining low does not regress", 100L, aligner.update(1, 20));
            Assertions.assertFalse("active again", aligner.isIdle(1));
            Assertions.assertEquals("all idle keeps last", 100L, aligner.markIdle(0));
        });

        Assertions.test("aligner validates partitions", () -> {
            Assertions.assertThrows("zero", IllegalArgumentException.class, () -> new WatermarkAligner(0));
            Assertions.assertThrows("unknown", IllegalArgumentException.class,
                () -> new WatermarkAligner(1).update(4, 1));
        });
    }
}
