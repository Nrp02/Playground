package classfile;

public final class ByteReader {
    private final byte[] data;
    private int position;

    public ByteReader(byte[] data) {
        this.data = data.clone();
        this.position = 0;
    }

    public int position() {
        return position;
    }

    public void seek(int target) {
        if (target < 0 || target > data.length) {
            throw new ClassFormatException("seek out of range: " + target);
        }
        position = target;
    }

    public int remaining() {
        return data.length - position;
    }

    public boolean hasRemaining() {
        return position < data.length;
    }

    private void require(int count) {
        if (count < 0 || remaining() < count) {
            throw new ClassFormatException(
                "truncated class file: needed " + count + " byte(s) at offset " + position
                    + " but only " + remaining() + " remain");
        }
    }

    public int u1() {
        require(1);
        return data[position++] & 0xFF;
    }

    public int s1() {
        return (byte) u1();
    }

    public int u2() {
        require(2);
        int high = data[position++] & 0xFF;
        int low = data[position++] & 0xFF;
        return (high << 8) | low;
    }

    public int s2() {
        return (short) u2();
    }

    public int s4() {
        require(4);
        int value = 0;
        for (int i = 0; i < 4; i++) {
            value = (value << 8) | (data[position++] & 0xFF);
        }
        return value;
    }

    public long u4() {
        return s4() & 0xFFFFFFFFL;
    }

    public long s8() {
        return ((long) s4() << 32) | (s4() & 0xFFFFFFFFL);
    }

    public byte[] bytes(int count) {
        require(count);
        byte[] out = new byte[count];
        System.arraycopy(data, position, out, 0, count);
        position += count;
        return out;
    }

    public void align(int base, int boundary) {
        while ((position - base) % boundary != 0) {
            u1();
        }
    }
}
