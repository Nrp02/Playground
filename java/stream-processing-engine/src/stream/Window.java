package stream;

public record Window(long start, long end) implements Comparable<Window> {

    public Window {
        if (end <= start) {
            throw new IllegalArgumentException("window end " + end + " must be after start " + start);
        }
    }

    public long maxTimestamp() {
        return end - 1;
    }

    public long length() {
        return end - start;
    }

    public boolean contains(long timestamp) {
        return timestamp >= start && timestamp < end;
    }

    public boolean intersects(Window other) {
        return start <= other.end && end >= other.start;
    }

    public Window cover(Window other) {
        return new Window(Math.min(start, other.start), Math.max(end, other.end));
    }

    @Override
    public int compareTo(Window other) {
        int byStart = Long.compare(start, other.start);
        return byStart != 0 ? byStart : Long.compare(end, other.end);
    }

    @Override
    public String toString() {
        return "[" + start + ", " + end + ")";
    }
}
