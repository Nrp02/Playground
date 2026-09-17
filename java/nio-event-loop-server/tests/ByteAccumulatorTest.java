import java.nio.ByteBuffer;
import java.util.Arrays;
import nio.ByteAccumulator;

public final class ByteAccumulatorTest {

    private ByteAccumulatorTest() {
    }

    public static void run() {
        Assertions.suite("byte accumulator");

        Assertions.test("appends, peeks and reads big-endian ints", () -> {
            ByteAccumulator acc = new ByteAccumulator(4);
            acc.append(ByteBuffer.wrap(new byte[] {0, 0, 1, 2, 9}));
            Assertions.assertEquals("readable", 5, acc.readableBytes());
            Assertions.assertEquals("int", 258, acc.getInt(0));
            Assertions.assertEquals("byte", (byte) 9, acc.getByte(4));
            acc.skip(4);
            Assertions.assertEquals("after skip", 1, acc.readableBytes());
            Assertions.assertTrue("rest", Arrays.equals(new byte[] {9}, acc.readBytes(1)));
            Assertions.assertEquals("drained", 0, acc.readableBytes());
        });

        Assertions.test("compacts before growing", () -> {
            ByteAccumulator acc = new ByteAccumulator(8);
            acc.append(new byte[] {1, 2, 3, 4, 5, 6});
            acc.skip(5);
            acc.append(new byte[] {7, 8, 9, 10, 11, 12});
            Assertions.assertEquals("capacity unchanged", 8, acc.capacity());
            Assertions.assertTrue("content", Arrays.equals(new byte[] {6, 7, 8, 9, 10, 11, 12}, acc.readBytes(7)));
        });

        Assertions.test("grows when compaction is not enough", () -> {
            ByteAccumulator acc = new ByteAccumulator(4);
            byte[] big = new byte[1000];
            for (int i = 0; i < big.length; i++) {
                big[i] = (byte) i;
            }
            acc.append(big);
            Assertions.assertTrue("grew", acc.capacity() >= 1000);
            Assertions.assertTrue("content", Arrays.equals(big, acc.readBytes(1000)));
        });

        Assertions.test("indexOf honours offsets and bounds are checked", () -> {
            ByteAccumulator acc = new ByteAccumulator();
            acc.append(new byte[] {'a', '\n', 'b', '\n'});
            Assertions.assertEquals("first", 1, acc.indexOf((byte) '\n', 0));
            Assertions.assertEquals("second", 3, acc.indexOf((byte) '\n', 2));
            Assertions.assertEquals("missing", -1, acc.indexOf((byte) 'z', 0));
            Assertions.assertThrows("int past end", IndexOutOfBoundsException.class, () -> acc.getInt(1));
            Assertions.assertThrows("read past end", IndexOutOfBoundsException.class, () -> acc.readBytes(5));
        });
    }
}
