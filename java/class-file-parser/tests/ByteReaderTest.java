import classfile.ByteReader;
import classfile.ClassFormatException;

public final class ByteReaderTest {

    private ByteReaderTest() {
    }

    public static void run() {
        Assertions.suite("ByteReader");

        Assertions.test("reads unsigned bytes and shorts big-endian", () -> {
            ByteReader reader = new ByteReader(new byte[]{(byte) 0xFF, (byte) 0xCA, (byte) 0xFE});
            Assertions.assertEquals("u1", 255, reader.u1());
            Assertions.assertEquals("u2", 0xCAFE, reader.u2());
            Assertions.assertFalse("exhausted", reader.hasRemaining());
        });

        Assertions.test("reads the class file magic as u4", () -> {
            ByteReader reader = new ByteReader(
                new byte[]{(byte) 0xCA, (byte) 0xFE, (byte) 0xBA, (byte) 0xBE});
            Assertions.assertEquals("magic", 0xCAFEBABEL, reader.u4());
        });

        Assertions.test("sign extends s1 s2 and s8", () -> {
            ByteReader reader = new ByteReader(new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFE});
            Assertions.assertEquals("s1", -1, reader.s1());
            Assertions.assertEquals("s2", -2, reader.s2());
            ByteReader wide = new ByteReader(new byte[]{-1, -1, -1, -1, -1, -1, -1, -1});
            Assertions.assertEquals("s8", -1L, wide.s8());
        });

        Assertions.test("rejects reads past the end", () -> Assertions.assertThrows(
            "truncated read must throw", ClassFormatException.class,
            () -> new ByteReader(new byte[]{1}).u2()));

        Assertions.test("seek and position track each other", () -> {
            ByteReader reader = new ByteReader(new byte[]{1, 2, 3, 4, 5});
            reader.bytes(3);
            Assertions.assertEquals("position", 3, reader.position());
            reader.seek(1);
            Assertions.assertEquals("after seek", 2, reader.u1());
            Assertions.assertEquals("remaining", 3, reader.remaining());
        });

        Assertions.test("rejects a seek outside the buffer", () -> Assertions.assertThrows(
            "bad seek must throw", ClassFormatException.class,
            () -> new ByteReader(new byte[]{1}).seek(9)));

        Assertions.test("align skips to the next boundary", () -> {
            ByteReader reader = new ByteReader(new byte[]{1, 2, 3, 4, 5, 6, 7, 8});
            reader.u1();
            reader.align(0, 4);
            Assertions.assertEquals("aligned position", 4, reader.position());
            reader.align(0, 4);
            Assertions.assertEquals("already aligned", 4, reader.position());
        });

        Assertions.test("defensively copies its input", () -> {
            byte[] source = {7, 8};
            ByteReader reader = new ByteReader(source);
            source[0] = 99;
            Assertions.assertEquals("copy is unaffected", 7, reader.u1());
        });
    }
}
