package nio;

import java.util.concurrent.atomic.AtomicLong;

public final class ServerStats {
    private final AtomicLong accepted = new AtomicLong();
    private final AtomicLong rejected = new AtomicLong();
    private final AtomicLong closed = new AtomicLong();
    private final AtomicLong bytesRead = new AtomicLong();
    private final AtomicLong bytesWritten = new AtomicLong();
    private final AtomicLong messagesIn = new AtomicLong();
    private final AtomicLong messagesOut = new AtomicLong();
    private final AtomicLong protocolErrors = new AtomicLong();
    private final AtomicLong idleTimeouts = new AtomicLong();
    private final AtomicLong writeStalls = new AtomicLong();
    private final AtomicLong droppedWrites = new AtomicLong();
    private final AtomicLong backpressureEvents = new AtomicLong();
    private final AtomicLong handlerFailures = new AtomicLong();

    void onAccepted() {
        accepted.incrementAndGet();
    }

    void onRejected() {
        rejected.incrementAndGet();
    }

    void onClosed() {
        closed.incrementAndGet();
    }

    void onRead(long bytes) {
        bytesRead.addAndGet(bytes);
    }

    void onWritten(long bytes) {
        bytesWritten.addAndGet(bytes);
    }

    void onMessageIn() {
        messagesIn.incrementAndGet();
    }

    void onMessageOut() {
        messagesOut.incrementAndGet();
    }

    void onProtocolError() {
        protocolErrors.incrementAndGet();
    }

    void onIdleTimeout() {
        idleTimeouts.incrementAndGet();
    }

    void onWriteStall() {
        writeStalls.incrementAndGet();
    }

    void onDroppedWrite() {
        droppedWrites.incrementAndGet();
    }

    void onBackpressure() {
        backpressureEvents.incrementAndGet();
    }

    void onHandlerFailure() {
        handlerFailures.incrementAndGet();
    }

    public long handlerFailures() {
        return handlerFailures.get();
    }

    public long accepted() {
        return accepted.get();
    }

    public long rejected() {
        return rejected.get();
    }

    public long closed() {
        return closed.get();
    }

    public long bytesRead() {
        return bytesRead.get();
    }

    public long bytesWritten() {
        return bytesWritten.get();
    }

    public long messagesIn() {
        return messagesIn.get();
    }

    public long messagesOut() {
        return messagesOut.get();
    }

    public long protocolErrors() {
        return protocolErrors.get();
    }

    public long idleTimeouts() {
        return idleTimeouts.get();
    }

    public long writeStalls() {
        return writeStalls.get();
    }

    public long droppedWrites() {
        return droppedWrites.get();
    }

    public long backpressureEvents() {
        return backpressureEvents.get();
    }

    @Override
    public String toString() {
        return "accepted=" + accepted() + " rejected=" + rejected() + " closed=" + closed()
            + " in=" + messagesIn() + " out=" + messagesOut()
            + " bytesRead=" + bytesRead() + " bytesWritten=" + bytesWritten()
            + " protocolErrors=" + protocolErrors() + " idleTimeouts=" + idleTimeouts()
            + " writeStalls=" + writeStalls() + " backpressure=" + backpressureEvents()
            + " dropped=" + droppedWrites();
    }
}
