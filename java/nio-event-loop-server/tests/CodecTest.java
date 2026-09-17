import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import nio.LengthPrefixedFrameCodec;
import nio.LineFrameCodec;
import nio.Pipeline;
import nio.ProtocolException;
import nio.Utf8StringCodec;

public final class CodecTest {

    private CodecTest() {
    }

    public static void run() {
        Assertions.suite("codecs and pipeline");

        Assertions.test("line codec reassembles lines split across chunks", () -> {
            Pipeline pipeline = Pipeline.of(new LineFrameCodec(64), new Utf8StringCodec());
            Assertions.assertEquals("partial", List.of(), pipeline.inbound(bytes("hel")));
            Assertions.assertEquals("completes", List.of("hello", "wor"), pipeline.inbound(bytes("lo\nwor\n")));
            Assertions.assertEquals("crlf stripped", List.of("x", ""), pipeline.inbound(bytes("x\r\n\n")));
        });

        Assertions.test("line codec rejects oversize lines with or without a newline", () -> {
            LineFrameCodec bounded = new LineFrameCodec(4);
            Assertions.assertThrows("no newline", ProtocolException.class,
                () -> bounded.decode(bytes("abcdefgh"), new ArrayList<>()));
            LineFrameCodec other = new LineFrameCodec(4);
            Assertions.assertThrows("with newline", ProtocolException.class,
                () -> other.decode(bytes("abcde\n"), new ArrayList<>()));
            LineFrameCodec exact = new LineFrameCodec(4);
            List<Object> out = new ArrayList<>();
            exact.decode(bytes("abcd\r\n"), out);
            Assertions.assertEquals("max length with CRLF accepted", 1, out.size());
        });

        Assertions.test("line codec encode appends newline and refuses embedded ones", () -> {
            Pipeline pipeline = Pipeline.of(new LineFrameCodec(64), new Utf8StringCodec());
            Assertions.assertEquals("framed", "héllo\n", text(pipeline.outbound("héllo")));
            Assertions.assertThrows("embedded newline", ProtocolException.class, () -> pipeline.outbound("a\nb"));
        });

        Assertions.test("length-prefixed codec handles byte-at-a-time delivery", () -> {
            LengthPrefixedFrameCodec codec = new LengthPrefixedFrameCodec(1024);
            ByteBuffer encoded = (ByteBuffer) codec.encode("payload".getBytes(StandardCharsets.UTF_8));
            List<Object> out = new ArrayList<>();
            while (encoded.hasRemaining()) {
                codec.decode(ByteBuffer.wrap(new byte[] {encoded.get()}), out);
            }
            Assertions.assertEquals("one frame", 1, out.size());
            Assertions.assertEquals("payload", "payload", new String((byte[]) out.get(0), StandardCharsets.UTF_8));
            Assertions.assertEquals("nothing buffered", 0, codec.bufferedBytes());
        });

        Assertions.test("length-prefixed codec decodes several frames in one chunk", () -> {
            Pipeline pipeline = Pipeline.of(new LengthPrefixedFrameCodec(1024), new Utf8StringCodec());
            ByteBuffer joined = ByteBuffer.allocate(64);
            joined.put(pipeline.outbound("one")).put(pipeline.outbound("")).put(pipeline.outbound("three"));
            joined.putInt(10).put((byte) 1).flip();
            Assertions.assertEquals("frames", List.of("one", "", "three"), pipeline.inbound(joined));
        });

        Assertions.test("length-prefixed codec rejects negative and oversize lengths", () -> {
            Assertions.assertThrows("negative", ProtocolException.class,
                () -> new LengthPrefixedFrameCodec(8).decode(ByteBuffer.allocate(4).putInt(-1).flip(), new ArrayList<>()));
            Assertions.assertThrows("oversize inbound", ProtocolException.class,
                () -> new LengthPrefixedFrameCodec(8).decode(ByteBuffer.allocate(4).putInt(9).flip(), new ArrayList<>()));
            Assertions.assertThrows("oversize outbound", ProtocolException.class,
                () -> new LengthPrefixedFrameCodec(2).encode(new byte[3]));
        });

        Assertions.test("utf-8 codec rejects malformed input and wrong types", () -> {
            Pipeline pipeline = Pipeline.of(new LineFrameCodec(64), new Utf8StringCodec());
            Assertions.assertThrows("malformed", ProtocolException.class,
                () -> pipeline.inbound(ByteBuffer.wrap(new byte[] {(byte) 0xC3, (byte) 0x28, '\n'})));
            Assertions.assertThrows("not a string", ProtocolException.class, () -> pipeline.outbound(42));
        });

        Assertions.test("empty pipeline copies raw bytes in and passes bytes out", () -> {
            Pipeline pipeline = Pipeline.of();
            ByteBuffer source = ByteBuffer.wrap(new byte[] {1, 2, 3});
            List<Object> in = pipeline.inbound(source);
            source.array()[0] = 99;
            Assertions.assertTrue("copied", Arrays.equals(new byte[] {1, 2, 3}, (byte[]) in.get(0)));
            Assertions.assertEquals("bytes out", 2, pipeline.outbound(new byte[] {5, 6}).remaining());
            Assertions.assertThrows("strings not accepted", ProtocolException.class, () -> pipeline.outbound("x"));
        });

        Assertions.test("pipeline stages keep independent state per instance", () -> {
            Pipeline a = Pipeline.of(new LineFrameCodec(64), new Utf8StringCodec());
            Pipeline b = Pipeline.of(new LineFrameCodec(64), new Utf8StringCodec());
            a.inbound(bytes("left-"));
            Assertions.assertEquals("b unaffected", List.of("right"), b.inbound(bytes("right\n")));
            Assertions.assertEquals("a continues", List.of("left-over"), a.inbound(bytes("over\n")));
        });
    }

    private static ByteBuffer bytes(String text) {
        return ByteBuffer.wrap(text.getBytes(StandardCharsets.UTF_8));
    }

    private static String text(ByteBuffer buffer) {
        byte[] out = new byte[buffer.remaining()];
        buffer.get(out);
        return new String(out, StandardCharsets.UTF_8);
    }
}
