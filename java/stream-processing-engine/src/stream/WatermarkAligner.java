package stream;

import java.util.Map;
import java.util.TreeMap;

public final class WatermarkAligner {
    private final Map<Integer, Long> watermarks = new TreeMap<>();
    private final Map<Integer, Boolean> idle = new TreeMap<>();
    private long emitted = Long.MIN_VALUE;

    public WatermarkAligner(int partitions) {
        if (partitions <= 0) {
            throw new IllegalArgumentException("need at least one partition");
        }
        for (int partition = 0; partition < partitions; partition++) {
            watermarks.put(partition, Long.MIN_VALUE);
            idle.put(partition, false);
        }
    }

    public long update(int partition, long watermark) {
        requirePartition(partition);
        idle.put(partition, false);
        watermarks.merge(partition, watermark, Math::max);
        return recompute();
    }

    public long markIdle(int partition) {
        requirePartition(partition);
        idle.put(partition, true);
        return recompute();
    }

    public long current() {
        return emitted;
    }

    public long partitionWatermark(int partition) {
        requirePartition(partition);
        return watermarks.get(partition);
    }

    public boolean isIdle(int partition) {
        requirePartition(partition);
        return idle.get(partition);
    }

    private long recompute() {
        long min = Long.MAX_VALUE;
        boolean anyActive = false;
        for (Map.Entry<Integer, Long> entry : watermarks.entrySet()) {
            if (idle.get(entry.getKey())) {
                continue;
            }
            anyActive = true;
            min = Math.min(min, entry.getValue());
        }
        if (anyActive && min > emitted) {
            emitted = min;
        }
        return emitted;
    }

    private void requirePartition(int partition) {
        if (!watermarks.containsKey(partition)) {
            throw new IllegalArgumentException("unknown partition " + partition);
        }
    }
}
