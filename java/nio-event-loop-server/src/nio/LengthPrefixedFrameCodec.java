package nio;

import java.nio.ByteBuffer;
import java.util.List;

public final class LengthPrefixedFrameCodec implements Codec {
    private final int maxFrameLength;
    private final ByteAccumulator buffer = new ByteAccumulator();

    public LengthPrefixedFrameCodec(int maxFrameLength) {
        if (maxFrameLength <= 0) {
            throw new IllegalArgumentException("maxFrameLength must be positive");
        }
        this.maxFrameLength = maxFrameLength;
    }

    @Override
    public void decode(Object message, List<Object> out) throws ProtocolException {
        buffer.append(Codecs.requireBuffer("LengthPrefixedFrameCodec", message));
        while (buffer.readableBytes() >= Integer.BYTES) {
            int length = buffer.getInt(0);
            if (length < 0) {
                throw new ProtocolException("negative frame length " + length);
            }
            if (length > maxFrameLength) {
                throw new ProtocolException("frame length " + length + " exceeds " + maxFrameLength);
            }
            if (buffer.readableBytes() < Integer.BYTES + length) {
                return;
            }
            buffer.skip(Integer.BYTES);
            out.add(buffer.readBytes(length));
        }
    }

    @Override
    public Object encode(Object message) throws ProtocolException {
        byte[] bytes = Codecs.requireBytes("LengthPrefixedFrameCodec", message);
        if (bytes.length > maxFrameLength) {
            throw new ProtocolException("frame length " + bytes.length + " exceeds " + maxFrameLength);
        }
        ByteBuffer framed = ByteBuffer.allocate(Integer.BYTES + bytes.length);
        framed.putInt(bytes.length).put(bytes).flip();
        return framed;
    }

    public int bufferedBytes() {
        return buffer.readableBytes();
    }
}
