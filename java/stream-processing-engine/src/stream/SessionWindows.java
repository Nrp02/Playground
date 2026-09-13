package stream;

import java.util.List;

public final class SessionWindows implements WindowAssigner {
    private final long gap;

    private SessionWindows(long gap) {
        if (gap <= 0) {
            throw new IllegalArgumentException("session gap must be positive");
        }
        this.gap = gap;
    }

    public static SessionWindows withGap(long gap) {
        return new SessionWindows(gap);
    }

    @Override
    public List<Window> assign(long timestamp) {
        return List.of(new Window(timestamp, timestamp + gap));
    }

    @Override
    public boolean isMerging() {
        return true;
    }

    @Override
    public String describe() {
        return "session(gap=" + gap + ")";
    }
}
