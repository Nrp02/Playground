package stream;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;

public final class BoundedOutOfOrdernessWatermarks {
    private final long maxOutOfOrderness;
    private long maxTimestamp = Long.MIN_VALUE;

    public BoundedOutOfOrdernessWatermarks(long maxOutOfOrderness) {
        if (maxOutOfOrderness < 0) {
            throw new IllegalArgumentException("out-of-orderness bound must not be negative");
        }
        this.maxOutOfOrderness = maxOutOfOrderness;
    }

    public void onEvent(long timestamp) {
        maxTimestamp = Math.max(maxTimestamp, timestamp);
    }

    public long currentWatermark() {
        if (maxTimestamp == Long.MIN_VALUE) {
            return Long.MIN_VALUE;
        }
        return maxTimestamp - maxOutOfOrderness - 1;
    }

    public long maxTimestamp() {
        return maxTimestamp;
    }

    public void snapshot(DataOutputStream out) throws IOException {
        out.writeLong(maxTimestamp);
    }

    public void restore(DataInputStream in) throws IOException {
        maxTimestamp = in.readLong();
    }
}
