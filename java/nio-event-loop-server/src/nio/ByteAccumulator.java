package nio;

import java.nio.ByteBuffer;

public final class ByteAccumulator {
    private byte[] data;
    private int readIndex;
    private int writeIndex;

    public ByteAccumulator() {
        this(256);
    }

    public ByteAccumulator(int initialCapacity) {
        if (initialCapacity <= 0) {
            throw new IllegalArgumentException("initialCapacity must be positive");
        }
        data = new byte[initialCapacity];
    }

    public void append(ByteBuffer source) {
        int length = source.remaining();
        ensureWritable(length);
        source.get(data, writeIndex, length);
        writeIndex += length;
    }

    public void append(byte[] bytes) {
        ensureWritable(bytes.length);
        System.arraycopy(bytes, 0, data, writeIndex, bytes.length);
        writeIndex += bytes.length;
    }

    public int readableBytes() {
        return writeIndex - readIndex;
    }

    public int capacity() {
        return data.length;
    }

    public byte getByte(int offset) {
        checkRange(offset, 1);
        return data[readIndex + offset];
    }

    public int getInt(int offset) {
        checkRange(offset, 4);
        int i = readIndex + offset;
        return ((data[i] & 0xff) << 24)
            | ((data[i + 1] & 0xff) << 16)
            | ((data[i + 2] & 0xff) << 8)
            | (data[i + 3] & 0xff);
    }

    public int indexOf(byte value, int fromOffset) {
        if (fromOffset < 0) {
            throw new IndexOutOfBoundsException("negative offset " + fromOffset);
        }
        for (int i = readIndex + fromOffset; i < writeIndex; i++) {
            if (data[i] == value) {
                return i - readIndex;
            }
        }
        return -1;
    }

    public byte[] readBytes(int length) {
        checkRange(0, length);
        byte[] out = new byte[length];
        System.arraycopy(data, readIndex, out, 0, length);
        advance(length);
        return out;
    }

    public void skip(int length) {
        checkRange(0, length);
        advance(length);
    }

    private void advance(int length) {
        readIndex += length;
        if (readIndex == writeIndex) {
            readIndex = 0;
            writeIndex = 0;
        }
    }

    private void checkRange(int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > readableBytes()) {
            throw new IndexOutOfBoundsException(
                "range [" + offset + ", " + (offset + length) + ") outside readable " + readableBytes());
        }
    }

    private void ensureWritable(int length) {
        if (data.length - writeIndex >= length) {
            return;
        }
        int readable = readableBytes();
        if (data.length - readable >= length) {
            System.arraycopy(data, readIndex, data, 0, readable);
        } else {
            long wanted = Math.max((long) data.length * 2, (long) readable + length);
            if (wanted > Integer.MAX_VALUE - 8) {
                throw new IllegalStateException("accumulator cannot grow to " + wanted + " bytes");
            }
            byte[] grown = new byte[(int) wanted];
            System.arraycopy(data, readIndex, grown, 0, readable);
            data = grown;
        }
        readIndex = 0;
        writeIndex = readable;
    }
}
