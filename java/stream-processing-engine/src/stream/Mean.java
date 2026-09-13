package stream;

public record Mean(long sum, long count) {

    public static final Mean EMPTY = new Mean(0, 0);

    public Mean {
        if (count < 0) {
            throw new IllegalArgumentException("count must not be negative");
        }
    }

    public Mean plus(long value) {
        return new Mean(sum + value, count + 1);
    }

    public Mean combine(Mean other) {
        return new Mean(sum + other.sum, count + other.count);
    }

    public double value() {
        return count == 0 ? 0.0 : (double) sum / count;
    }
}
