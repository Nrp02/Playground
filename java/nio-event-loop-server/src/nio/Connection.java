package nio;

import java.io.IOException;
import java.net.SocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

public final class Connection {
    private static final AtomicLong IDS = new AtomicLong();
    private static final int MAX_READS_PER_EVENT = 16;

    private final long id = IDS.incrementAndGet();
    private final SocketChannel channel;
    private final EventLoop loop;
    private final Pipeline pipeline;
    private final Handler handler;
    private final ServerConfig config;
    private final BufferPool pool;
    private final ServerStats stats;
    private final Consumer<Connection> onClosed;
    private final ArrayDeque<ByteBuffer> outbound = new ArrayDeque<>();
    private final Map<String, Object> attributes = new ConcurrentHashMap<>();
    private final SocketAddress remoteAddress;
    private SelectionKey key;
    private Cancellable timeoutCheck;
    private boolean readPaused;
    private long lastReadNanos;
    private long lastWriteProgressNanos;
    private volatile long pendingBytes;
    private volatile boolean writable = true;
    private volatile boolean closing;
    private volatile boolean closed;

    Connection(SocketChannel channel, EventLoop loop, Pipeline pipeline, Handler handler, ServerConfig config,
               BufferPool pool, ServerStats stats, Consumer<Connection> onClosed) throws IOException {
        this.channel = channel;
        this.loop = loop;
        this.pipeline = pipeline;
        this.handler = handler;
        this.config = config;
        this.pool = pool;
        this.stats = stats;
        this.onClosed = onClosed;
        this.remoteAddress = channel.getRemoteAddress();
    }

    void open() {
        long now = System.nanoTime();
        lastReadNanos = now;
        lastWriteProgressNanos = now;
        try {
            key = loop.register(channel, SelectionKey.OP_READ, new Io());
        } catch (IOException e) {
            closeNow(e);
            return;
        }
        try {
            handler.onOpen(this);
        } catch (RuntimeException e) {
            closeNow(e);
            return;
        }
        Duration firstCheck = shortestTimeout();
        if (firstCheck != null && !closed) {
            timeoutCheck = loop.schedule(this::checkTimeouts, firstCheck);
        }
    }

    public long id() {
        return id;
    }

    public SocketAddress remoteAddress() {
        return remoteAddress;
    }

    public EventLoop loop() {
        return loop;
    }

    public boolean isOpen() {
        return !closed;
    }

    public boolean isClosing() {
        return closing && !closed;
    }

    public boolean isWritable() {
        return writable && !closed;
    }

    public long pendingBytes() {
        return pendingBytes;
    }

    public boolean isReadPaused() {
        return readPaused;
    }

    public void attribute(String name, Object value) {
        if (value == null) {
            attributes.remove(name);
        } else {
            attributes.put(name, value);
        }
    }

    public Object attribute(String name) {
        return attributes.get(name);
    }

    public void write(Object message) {
        Objects.requireNonNull(message, "message");
        onLoop(() -> doWrite(message));
    }

    public void writeAndClose(Object message) {
        Objects.requireNonNull(message, "message");
        onLoop(() -> {
            doWrite(message);
            doCloseGracefully();
        });
    }

    public void close() {
        onLoop(() -> closeNow(null));
    }

    public void closeGracefully() {
        onLoop(this::doCloseGracefully);
    }

    private void onLoop(Runnable action) {
        if (loop.inEventLoop()) {
            action.run();
            return;
        }
        try {
            loop.execute(action);
        } catch (RejectedExecutionException e) {
            closeNow(null);
        }
    }

    private void doWrite(Object message) {
        if (closed || closing) {
            stats.onDroppedWrite();
            return;
        }
        ByteBuffer bytes;
        try {
            bytes = pipeline.outbound(message);
        } catch (ProtocolException e) {
            closeNow(e);
            return;
        }
        stats.onMessageOut();
        if (!bytes.hasRemaining()) {
            return;
        }
        boolean wasEmpty = outbound.isEmpty();
        outbound.add(bytes);
        pendingBytes += bytes.remaining();
        if (wasEmpty) {
            lastWriteProgressNanos = System.nanoTime();
            flush();
        } else {
            updateWritability();
        }
    }

    private void doCloseGracefully() {
        if (closed) {
            return;
        }
        closing = true;
        setInterest(SelectionKey.OP_READ, false);
        if (outbound.isEmpty()) {
            closeNow(null);
        }
    }

    private void flush() {
        try {
            while (!outbound.isEmpty()) {
                ByteBuffer head = outbound.peek();
                int written = channel.write(head);
                if (written > 0) {
                    pendingBytes -= written;
                    stats.onWritten(written);
                    lastWriteProgressNanos = System.nanoTime();
                }
                if (head.hasRemaining()) {
                    setInterest(SelectionKey.OP_WRITE, true);
                    updateWritability();
                    return;
                }
                outbound.poll();
            }
        } catch (IOException e) {
            closeNow(e);
            return;
        }
        setInterest(SelectionKey.OP_WRITE, false);
        updateWritability();
        if (closing && !closed && outbound.isEmpty()) {
            closeNow(null);
        }
    }

    private void updateWritability() {
        if (closed) {
            return;
        }
        if (writable && pendingBytes > config.highWaterMark()) {
            writable = false;
            stats.onBackpressure();
            if (config.pauseReadsWhenUnwritable()) {
                readPaused = true;
                setInterest(SelectionKey.OP_READ, false);
            }
            notifyWritability(false);
        } else if (!writable && pendingBytes <= config.lowWaterMark()) {
            writable = true;
            if (readPaused) {
                readPaused = false;
                lastReadNanos = System.nanoTime();
                if (!closing) {
                    setInterest(SelectionKey.OP_READ, true);
                }
            }
            notifyWritability(true);
        }
    }

    private void notifyWritability(boolean state) {
        try {
            handler.onWritabilityChanged(this, state);
        } catch (RuntimeException e) {
            closeNow(e);
        }
    }

    private void read() {
        for (int round = 0; round < MAX_READS_PER_EVENT && !closed && !readPaused && !closing; round++) {
            ByteBuffer buffer = pool.acquire();
            int count;
            List<Object> messages;
            try {
                count = channel.read(buffer);
                if (count < 0) {
                    doCloseGracefully();
                    return;
                }
                if (count == 0) {
                    return;
                }
                lastReadNanos = System.nanoTime();
                stats.onRead(count);
                buffer.flip();
                messages = pipeline.inbound(buffer);
            } catch (IOException e) {
                closeNow(e);
                return;
            } finally {
                pool.release(buffer);
            }
            for (Object message : messages) {
                if (closed) {
                    return;
                }
                stats.onMessageIn();
                try {
                    handler.onMessage(this, message);
                } catch (RuntimeException e) {
                    closeNow(e);
                    return;
                }
            }
            if (count < pool.bufferSize()) {
                return;
            }
        }
    }

    private Duration shortestTimeout() {
        Duration idle = config.readIdleTimeout();
        Duration stall = config.writeStallTimeout();
        if (idle == null) {
            return stall;
        }
        if (stall == null) {
            return idle;
        }
        return idle.compareTo(stall) <= 0 ? idle : stall;
    }

    private void checkTimeouts() {
        if (closed) {
            return;
        }
        long now = System.nanoTime();
        long next = Long.MAX_VALUE;
        Duration idle = config.readIdleTimeout();
        if (idle != null) {
            if (readPaused || closing) {
                lastReadNanos = now;
            }
            long remaining = idle.toNanos() - (now - lastReadNanos);
            if (remaining <= 0) {
                stats.onIdleTimeout();
                closeNow(new ConnectionTimeoutException("no inbound data for " + idle.toMillis() + "ms"));
                return;
            }
            next = Math.min(next, remaining);
        }
        Duration stall = config.writeStallTimeout();
        if (stall != null) {
            if (outbound.isEmpty()) {
                next = Math.min(next, stall.toNanos());
            } else {
                long remaining = stall.toNanos() - (now - lastWriteProgressNanos);
                if (remaining <= 0) {
                    stats.onWriteStall();
                    closeNow(new ConnectionTimeoutException("no write progress for " + stall.toMillis() + "ms"));
                    return;
                }
                next = Math.min(next, remaining);
            }
        }
        if (next != Long.MAX_VALUE) {
            timeoutCheck = loop.schedule(this::checkTimeouts, Duration.ofNanos(next));
        }
    }

    private void setInterest(int op, boolean enabled) {
        if (key == null || !key.isValid()) {
            return;
        }
        int ops = key.interestOps();
        int updated = enabled ? ops | op : ops & ~op;
        if (updated != ops) {
            key.interestOps(updated);
        }
    }

    private void closeNow(Throwable cause) {
        if (closed) {
            return;
        }
        closed = true;
        closing = true;
        if (timeoutCheck != null) {
            timeoutCheck.cancel();
        }
        if (key != null) {
            key.cancel();
        }
        try {
            channel.close();
        } catch (IOException e) {
            if (cause == null) {
                cause = e;
            }
        }
        outbound.clear();
        pendingBytes = 0;
        if (cause instanceof ProtocolException) {
            stats.onProtocolError();
        }
        stats.onClosed();
        onClosed.accept(this);
        try {
            handler.onClose(this, cause);
        } catch (RuntimeException e) {
            stats.onHandlerFailure();
        }
    }

    @Override
    public String toString() {
        return "Connection#" + id + "[" + remoteAddress + (closed ? ", closed" : "") + "]";
    }

    private final class Io implements IoHandler {
        @Override
        public void onReady(SelectionKey selected) {
            if (selected.isValid() && selected.isWritable()) {
                flush();
            }
            if (selected.isValid() && selected.isReadable()) {
                read();
            }
        }

        @Override
        public void onFailure(Exception failure) {
            closeNow(failure);
        }

        @Override
        public void onLoopClosed() {
            closeNow(null);
        }
    }
}
