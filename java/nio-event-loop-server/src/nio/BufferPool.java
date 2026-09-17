package nio;

import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.Set;

public final class BufferPool {
    private final int bufferSize;
    private final int maxPooled;
    private final boolean direct;
    private final ArrayDeque<ByteBuffer> free = new ArrayDeque<>();
    private final Set<ByteBuffer> leased = Collections.newSetFromMap(new IdentityHashMap<>());
    private long allocations;
    private long reuses;

    public BufferPool(int bufferSize, int maxPooled, boolean direct) {
        if (bufferSize <= 0) {
            throw new IllegalArgumentException("bufferSize must be positive");
        }
        if (maxPooled < 0) {
            throw new IllegalArgumentException("maxPooled must not be negative");
        }
        this.bufferSize = bufferSize;
        this.maxPooled = maxPooled;
        this.direct = direct;
    }

    public synchronized ByteBuffer acquire() {
        ByteBuffer buffer = free.pollFirst();
        if (buffer == null) {
            buffer = direct ? ByteBuffer.allocateDirect(bufferSize) : ByteBuffer.allocate(bufferSize);
            allocations++;
        } else {
            reuses++;
        }
        leased.add(buffer);
        return buffer;
    }

    public synchronized void release(ByteBuffer buffer) {
        if (!leased.remove(buffer)) {
            throw new IllegalArgumentException("buffer is not currently leased from this pool");
        }
        buffer.clear();
        if (free.size() < maxPooled) {
            free.offerFirst(buffer);
        }
    }

    public synchronized boolean isLeased(ByteBuffer buffer) {
        return leased.contains(buffer);
    }

    public int bufferSize() {
        return bufferSize;
    }

    public synchronized int pooledCount() {
        return free.size();
    }

    public synchronized int leasedCount() {
        return leased.size();
    }

    public synchronized long allocations() {
        return allocations;
    }

    public synchronized long reuses() {
        return reuses;
    }
}
