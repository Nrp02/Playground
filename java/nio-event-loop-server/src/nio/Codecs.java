package nio;

import java.nio.ByteBuffer;

final class Codecs {

    private Codecs() {
    }

    static ByteBuffer requireBuffer(String stage, Object message) throws ProtocolException {
        if (message instanceof ByteBuffer buffer) {
            return buffer;
        }
        if (message instanceof byte[] bytes) {
            return ByteBuffer.wrap(bytes);
        }
        throw unsupported(stage, message);
    }

    static byte[] requireBytes(String stage, Object message) throws ProtocolException {
        if (message instanceof byte[] bytes) {
            return bytes;
        }
        if (message instanceof ByteBuffer buffer) {
            byte[] copy = new byte[buffer.remaining()];
            buffer.duplicate().get(copy);
            return copy;
        }
        throw unsupported(stage, message);
    }

    static ProtocolException unsupported(String stage, Object message) {
        String type = message == null ? "null" : message.getClass().getName();
        return new ProtocolException(stage + " cannot handle message of type " + type);
    }
}
