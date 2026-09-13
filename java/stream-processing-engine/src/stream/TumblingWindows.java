package stream;

import java.util.List;

public final class TumblingWindows implements WindowAssigner {
    private final long size;
    private final long offset;

    private TumblingWindows(long size, long offset) {
        if (size <= 0) {
            throw new IllegalArgumentException("tumbling window size must be positive");
        }
        this.size = size;
        this.offset = Math.floorMod(offset, size);
    }

    public static TumblingWindows of(long size) {
        return new TumblingWindows(size, 0);
    }

    public static TumblingWindows of(long size, long offset) {
        return new TumblingWindows(size, offset);
    }

    @Override
    public List<Window> assign(long timestamp) {
        long start = timestamp - Math.floorMod(timestamp - offset, size);
        return List.of(new Window(start, start + size));
    }

    @Override
    public boolean isMerging() {
        return false;
    }

    @Override
    public String describe() {
        return "tumbling(size=" + size + (offset == 0 ? "" : ", offset=" + offset) + ")";
    }
}
