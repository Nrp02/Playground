package actor;

import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

final class Mailbox {
    private final ArrayDeque<Envelope> queue = new ArrayDeque<>();
    private final int capacity;
    private final OverflowStrategy strategy;
    private final Duration blockTimeout;
    private final ReentrantLock lock = new ReentrantLock();
    private final Condition notFull = lock.newCondition();
    private boolean closed;
    private long dropped;

    Mailbox(int capacity, OverflowStrategy strategy, Duration blockTimeout) {
        this.capacity = capacity;
        this.strategy = strategy;
        this.blockTimeout = blockTimeout;
    }

    Envelope offer(Envelope envelope, String owner) {
        lock.lock();
        try {
            if (closed) {
                return envelope;
            }
            if (queue.size() < capacity) {
                queue.addLast(envelope);
                return null;
            }
            return switch (strategy) {
                case DROP_NEW -> {
                    dropped++;
                    yield envelope;
                }
                case DROP_OLDEST -> {
                    Envelope oldest = queue.pollFirst();
                    queue.addLast(envelope);
                    dropped++;
                    yield oldest;
                }
                case REJECT -> {
                    dropped++;
                    throw new MailboxOverflowException(owner, capacity);
                }
                case BLOCK -> awaitSpace(envelope, owner);
            };
        } finally {
            lock.unlock();
        }
    }

    private Envelope awaitSpace(Envelope envelope, String owner) {
        long remaining = blockTimeout.toNanos();
        while (queue.size() >= capacity && !closed) {
            if (remaining <= 0) {
                dropped++;
                throw new MailboxOverflowException(owner, capacity);
            }
            try {
                remaining = notFull.awaitNanos(remaining);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                dropped++;
                throw new MailboxOverflowException(owner, capacity);
            }
        }
        if (closed) {
            return envelope;
        }
        queue.addLast(envelope);
        return null;
    }

    Envelope poll() {
        lock.lock();
        try {
            Envelope envelope = queue.pollFirst();
            if (envelope != null) {
                notFull.signal();
            }
            return envelope;
        } finally {
            lock.unlock();
        }
    }

    boolean isEmpty() {
        lock.lock();
        try {
            return queue.isEmpty();
        } finally {
            lock.unlock();
        }
    }

    int size() {
        lock.lock();
        try {
            return queue.size();
        } finally {
            lock.unlock();
        }
    }

    long droppedCount() {
        lock.lock();
        try {
            return dropped;
        } finally {
            lock.unlock();
        }
    }

    List<Envelope> close() {
        lock.lock();
        try {
            closed = true;
            List<Envelope> remaining = new ArrayList<>(queue);
            queue.clear();
            notFull.signalAll();
            return remaining;
        } finally {
            lock.unlock();
        }
    }
}
