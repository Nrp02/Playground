package stream;

import java.util.ArrayList;
import java.util.List;

public final class SlidingWindows implements WindowAssigner {
    private final long size;
    private final long slide;

    private SlidingWindows(long size, long slide) {
        if (size <= 0 || slide <= 0) {
            throw new IllegalArgumentException("sliding window size and slide must be positive");
        }
        if (slide > size) {
            throw new IllegalArgumentException("slide " + slide + " larger than size " + size + " would leave gaps");
        }
        this.size = size;
        this.slide = slide;
    }

    public static SlidingWindows of(long size, long slide) {
        return new SlidingWindows(size, slide);
    }

    @Override
    public List<Window> assign(long timestamp) {
        long lastStart = timestamp - Math.floorMod(timestamp, slide);
        List<Window> windows = new ArrayList<>();
        for (long start = lastStart; start > timestamp - size; start -= slide) {
            windows.add(0, new Window(start, start + size));
        }
        return windows;
    }

    @Override
    public boolean isMerging() {
        return false;
    }

    @Override
    public String describe() {
        return "sliding(size=" + size + ", slide=" + slide + ")";
    }
}
