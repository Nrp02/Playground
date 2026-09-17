package nio;

import java.time.Duration;
import java.util.Objects;
import java.util.function.Supplier;

public record ServerConfig(
    String host,
    int port,
    int backlog,
    int workerThreads,
    int maxConnections,
    int readBufferSize,
    int maxPooledBuffers,
    long highWaterMark,
    long lowWaterMark,
    boolean pauseReadsWhenUnwritable,
    Duration readIdleTimeout,
    Duration writeStallTimeout,
    int socketSendBuffer,
    Supplier<Pipeline> pipelineFactory,
    Supplier<Handler> handlerFactory) {

    public ServerConfig {
        Objects.requireNonNull(host, "host");
        Objects.requireNonNull(pipelineFactory, "pipelineFactory");
        Objects.requireNonNull(handlerFactory, "handlerFactory");
        if (port < 0 || port > 65535) {
            throw new IllegalArgumentException("port out of range: " + port);
        }
        if (backlog <= 0 || workerThreads <= 0 || maxConnections <= 0 || readBufferSize <= 0) {
            throw new IllegalArgumentException("backlog, workerThreads, maxConnections and readBufferSize must be positive");
        }
        if (maxPooledBuffers < 0 || socketSendBuffer < 0) {
            throw new IllegalArgumentException("maxPooledBuffers and socketSendBuffer must not be negative");
        }
        if (lowWaterMark < 0 || highWaterMark <= 0 || lowWaterMark > highWaterMark) {
            throw new IllegalArgumentException("require 0 <= lowWaterMark <= highWaterMark and highWaterMark > 0");
        }
        requirePositive("readIdleTimeout", readIdleTimeout);
        requirePositive("writeStallTimeout", writeStallTimeout);
    }

    private static void requirePositive(String name, Duration duration) {
        if (duration != null && (duration.isNegative() || duration.isZero())) {
            throw new IllegalArgumentException(name + " must be positive");
        }
    }

    public static Builder builder(Supplier<Handler> handlerFactory) {
        return new Builder(handlerFactory);
    }

    public static final class Builder {
        private String host = "127.0.0.1";
        private int port;
        private int backlog = 128;
        private int workerThreads = 2;
        private int maxConnections = 1024;
        private int readBufferSize = 16 * 1024;
        private int maxPooledBuffers = 64;
        private long highWaterMark = 256 * 1024;
        private long lowWaterMark = 128 * 1024;
        private boolean pauseReadsWhenUnwritable = true;
        private Duration readIdleTimeout;
        private Duration writeStallTimeout;
        private int socketSendBuffer;
        private Supplier<Pipeline> pipelineFactory = Pipeline::of;
        private final Supplier<Handler> handlerFactory;

        private Builder(Supplier<Handler> handlerFactory) {
            this.handlerFactory = handlerFactory;
        }

        public Builder host(String value) {
            host = value;
            return this;
        }

        public Builder port(int value) {
            port = value;
            return this;
        }

        public Builder backlog(int value) {
            backlog = value;
            return this;
        }

        public Builder workerThreads(int value) {
            workerThreads = value;
            return this;
        }

        public Builder maxConnections(int value) {
            maxConnections = value;
            return this;
        }

        public Builder readBufferSize(int value) {
            readBufferSize = value;
            return this;
        }

        public Builder maxPooledBuffers(int value) {
            maxPooledBuffers = value;
            return this;
        }

        public Builder waterMarks(long low, long high) {
            lowWaterMark = low;
            highWaterMark = high;
            return this;
        }

        public Builder pauseReadsWhenUnwritable(boolean value) {
            pauseReadsWhenUnwritable = value;
            return this;
        }

        public Builder readIdleTimeout(Duration value) {
            readIdleTimeout = value;
            return this;
        }

        public Builder writeStallTimeout(Duration value) {
            writeStallTimeout = value;
            return this;
        }

        public Builder socketSendBuffer(int value) {
            socketSendBuffer = value;
            return this;
        }

        public Builder pipeline(Supplier<Pipeline> value) {
            pipelineFactory = value;
            return this;
        }

        public ServerConfig build() {
            return new ServerConfig(host, port, backlog, workerThreads, maxConnections, readBufferSize,
                maxPooledBuffers, highWaterMark, lowWaterMark, pauseReadsWhenUnwritable, readIdleTimeout,
                writeStallTimeout, socketSendBuffer, pipelineFactory, handlerFactory);
        }
    }
}
