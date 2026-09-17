package nio;

import java.nio.ByteBuffer;
import java.util.List;

public final class LineFrameCodec implements Codec {
    private static final byte LF = '\n';
    private static final byte CR = '\r';

    private final int maxLineLength;
    private final ByteAccumulator buffer = new ByteAccumulator();
    private int scanned;

    public LineFrameCodec(int maxLineLength) {
        if (maxLineLength <= 0) {
            throw new IllegalArgumentException("maxLineLength must be positive");
        }
        this.maxLineLength = maxLineLength;
    }

    @Override
    public void decode(Object message, List<Object> out) throws ProtocolException {
        buffer.append(Codecs.requireBuffer("LineFrameCodec", message));
        while (true) {
            int newline = buffer.indexOf(LF, scanned);
            if (newline < 0) {
                scanned = buffer.readableBytes();
                if (scanned > maxLineLength + 1) {
                    throw new ProtocolException("line exceeds " + maxLineLength + " bytes");
                }
                return;
            }
            int length = newline;
            if (length > 0 && buffer.getByte(length - 1) == CR) {
                length--;
            }
            if (length > maxLineLength) {
                throw new ProtocolException("line exceeds " + maxLineLength + " bytes");
            }
            out.add(buffer.readBytes(length));
            buffer.skip(newline - length + 1);
            scanned = 0;
        }
    }

    @Override
    public Object encode(Object message) throws ProtocolException {
        byte[] bytes = Codecs.requireBytes("LineFrameCodec", message);
        if (bytes.length > maxLineLength) {
            throw new ProtocolException("line exceeds " + maxLineLength + " bytes");
        }
        for (byte b : bytes) {
            if (b == LF) {
                throw new ProtocolException("outbound line contains a newline");
            }
        }
        ByteBuffer framed = ByteBuffer.allocate(bytes.length + 1);
        framed.put(bytes).put(LF).flip();
        return framed;
    }

    public int bufferedBytes() {
        return buffer.readableBytes();
    }
}
