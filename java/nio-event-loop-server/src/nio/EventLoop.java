package nio;

import java.io.IOException;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.SelectableChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Objects;
import java.util.PriorityQueue;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

public final class EventLoop implements Executor {
    private static final int MAX_TASKS_PER_TICK = 1024;

    private final Selector selector;
    private final Thread thread;
    private final Queue<Runnable> tasks = new ConcurrentLinkedQueue<>();
    private final PriorityQueue<ScheduledTask> timers = new PriorityQueue<>();
    private final AtomicBoolean wakeupPending = new AtomicBoolean();
    private final AtomicBoolean started = new AtomicBoolean();
    private final CountDownLatch terminated = new CountDownLatch(1);
    private final AtomicLong sequence = new AtomicLong();
    private final AtomicLong taskFailures = new AtomicLong();
    private volatile boolean shuttingDown;
    private volatile Throwable lastFailure;

    public EventLoop(String name) throws IOException {
        selector = Selector.open();
        thread = new Thread(this::run, name);
        thread.setDaemon(true);
    }

    public void start() {
        if (!started.compareAndSet(false, true)) {
            throw new IllegalStateException("event loop already started");
        }
        thread.start();
    }

    public boolean inEventLoop() {
        return Thread.currentThread() == thread;
    }

    public boolean isTerminated() {
        return terminated.getCount() == 0;
    }

    @Override
    public void execute(Runnable task) {
        Objects.requireNonNull(task, "task");
        if (isTerminated()) {
            throw new RejectedExecutionException("event loop " + thread.getName() + " has terminated");
        }
        tasks.add(task);
        if (isTerminated() && tasks.remove(task)) {
            throw new RejectedExecutionException("event loop " + thread.getName() + " has terminated");
        }
        if (!inEventLoop()) {
            wakeup();
        }
    }

    public Cancellable schedule(Runnable task, Duration delay) {
        Objects.requireNonNull(task, "task");
        long delayNanos = Math.max(0, delay.toNanos());
        ScheduledTask scheduled = new ScheduledTask(System.nanoTime() + delayNanos, sequence.getAndIncrement(), task);
        if (inEventLoop()) {
            timers.add(scheduled);
        } else {
            execute(() -> timers.add(scheduled));
        }
        return scheduled;
    }

    SelectionKey register(SelectableChannel channel, int ops, IoHandler handler) throws ClosedChannelException {
        if (!inEventLoop()) {
            throw new IllegalStateException("channels must be registered from the event loop thread");
        }
        return channel.register(selector, ops, handler);
    }

    public void shutdown() {
        shuttingDown = true;
        if (!started.get()) {
            started.set(true);
            closeSelector();
            terminated.countDown();
            return;
        }
        wakeup();
    }

    public boolean awaitTermination(Duration timeout) throws InterruptedException {
        return terminated.await(timeout.toNanos(), TimeUnit.NANOSECONDS);
    }

    public long taskFailures() {
        return taskFailures.get();
    }

    public Throwable lastFailure() {
        return lastFailure;
    }

    public int pendingTimers() {
        return timers.size();
    }

    public String name() {
        return thread.getName();
    }

    private void wakeup() {
        if (wakeupPending.compareAndSet(false, true)) {
            selector.wakeup();
        }
    }

    private void run() {
        try {
            while (!shuttingDown) {
                long timeoutMillis = nextTimeoutMillis();
                int ready;
                if (!tasks.isEmpty() || timeoutMillis == 0) {
                    ready = selector.selectNow();
                } else {
                    ready = selector.select(Math.max(0, timeoutMillis));
                }
                wakeupPending.set(false);
                if (ready > 0) {
                    processSelectedKeys();
                }
                runTimers();
                runTasks(MAX_TASKS_PER_TICK);
            }
        } catch (IOException | RuntimeException e) {
            lastFailure = e;
        } finally {
            runTasks(Integer.MAX_VALUE);
            List<SelectionKey> keys = new ArrayList<>(selector.keys());
            for (SelectionKey key : keys) {
                if (key.attachment() instanceof IoHandler handler) {
                    try {
                        handler.onLoopClosed();
                    } catch (RuntimeException e) {
                        recordFailure(e);
                    }
                }
            }
            runTasks(Integer.MAX_VALUE);
            closeSelector();
            terminated.countDown();
        }
    }

    private void closeSelector() {
        try {
            selector.close();
        } catch (IOException e) {
            lastFailure = e;
        }
    }

    private long nextTimeoutMillis() {
        while (!timers.isEmpty() && timers.peek().isCancelled()) {
            timers.poll();
        }
        ScheduledTask next = timers.peek();
        if (next == null) {
            return -1;
        }
        long remaining = next.deadlineNanos - System.nanoTime();
        if (remaining <= 0) {
            return 0;
        }
        return Math.max(1, TimeUnit.NANOSECONDS.toMillis(remaining + 999_999));
    }

    private void processSelectedKeys() {
        Iterator<SelectionKey> iterator = selector.selectedKeys().iterator();
        while (iterator.hasNext()) {
            SelectionKey key = iterator.next();
            iterator.remove();
            if (!key.isValid() || !(key.attachment() instanceof IoHandler handler)) {
                continue;
            }
            try {
                handler.onReady(key);
            } catch (IOException | RuntimeException e) {
                handler.onFailure(e);
            }
        }
    }

    private void runTimers() {
        long now = System.nanoTime();
        while (!timers.isEmpty()) {
            ScheduledTask head = timers.peek();
            if (head.isCancelled()) {
                timers.poll();
                continue;
            }
            if (head.deadlineNanos > now) {
                return;
            }
            timers.poll();
            if (head.fire()) {
                safeRun(head.task);
            }
        }
    }

    private void runTasks(int limit) {
        for (int i = 0; i < limit; i++) {
            Runnable task = tasks.poll();
            if (task == null) {
                return;
            }
            safeRun(task);
        }
    }

    private void safeRun(Runnable task) {
        try {
            task.run();
        } catch (RuntimeException e) {
            recordFailure(e);
        }
    }

    private void recordFailure(RuntimeException e) {
        taskFailures.incrementAndGet();
        lastFailure = e;
    }

    private static final class ScheduledTask implements Cancellable, Comparable<ScheduledTask> {
        private final long deadlineNanos;
        private final long sequence;
        private final Runnable task;
        private final AtomicBoolean done = new AtomicBoolean();
        private volatile boolean cancelled;

        private ScheduledTask(long deadlineNanos, long sequence, Runnable task) {
            this.deadlineNanos = deadlineNanos;
            this.sequence = sequence;
            this.task = task;
        }

        @Override
        public boolean cancel() {
            if (done.compareAndSet(false, true)) {
                cancelled = true;
                return true;
            }
            return false;
        }

        @Override
        public boolean isCancelled() {
            return cancelled;
        }

        private boolean fire() {
            return done.compareAndSet(false, true);
        }

        @Override
        public int compareTo(ScheduledTask other) {
            int byDeadline = Long.compare(deadlineNanos, other.deadlineNanos);
            return byDeadline != 0 ? byDeadline : Long.compare(sequence, other.sequence);
        }
    }
}
