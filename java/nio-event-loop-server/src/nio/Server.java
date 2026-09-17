package nio;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.StandardSocketOptions;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.time.Duration;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public final class Server {
    private final ServerConfig config;
    private final EventLoop acceptLoop;
    private final EventLoop[] workers;
    private final ServerSocketChannel serverChannel;
    private final Set<Connection> connections = ConcurrentHashMap.newKeySet();
    private final ServerStats stats = new ServerStats();
    private final BufferPool pool;
    private final AtomicInteger nextWorker = new AtomicInteger();
    private final AtomicBoolean shutdownStarted = new AtomicBoolean();
    private final int port;
    private volatile boolean accepting = true;

    private Server(ServerConfig config) throws IOException {
        this.config = config;
        this.pool = new BufferPool(config.readBufferSize(), config.maxPooledBuffers(), false);
        this.acceptLoop = new EventLoop("nio-accept");
        this.workers = new EventLoop[config.workerThreads()];
        for (int i = 0; i < workers.length; i++) {
            workers[i] = new EventLoop("nio-worker-" + i);
        }
        this.serverChannel = ServerSocketChannel.open();
        try {
            serverChannel.setOption(StandardSocketOptions.SO_REUSEADDR, true);
            serverChannel.bind(new InetSocketAddress(config.host(), config.port()), config.backlog());
            serverChannel.configureBlocking(false);
            this.port = ((InetSocketAddress) serverChannel.getLocalAddress()).getPort();
        } catch (IOException e) {
            serverChannel.close();
            throw e;
        }
    }

    public static Server start(ServerConfig config) throws IOException {
        Server server = new Server(config);
        server.begin();
        return server;
    }

    private void begin() throws IOException {
        acceptLoop.start();
        for (EventLoop worker : workers) {
            worker.start();
        }
        CompletableFuture<Void> registered = new CompletableFuture<>();
        acceptLoop.execute(() -> {
            try {
                acceptLoop.register(serverChannel, SelectionKey.OP_ACCEPT, new Acceptor());
                registered.complete(null);
            } catch (IOException | RuntimeException e) {
                registered.completeExceptionally(e);
            }
        });
        try {
            registered.get(5, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            abort();
            throw new IOException("interrupted while starting server", e);
        } catch (ExecutionException | TimeoutException e) {
            abort();
            throw new IOException("failed to register acceptor", e);
        }
    }

    private void abort() throws IOException {
        acceptLoop.shutdown();
        for (EventLoop worker : workers) {
            worker.shutdown();
        }
        serverChannel.close();
    }

    public int port() {
        return port;
    }

    public ServerStats stats() {
        return stats;
    }

    public BufferPool bufferPool() {
        return pool;
    }

    public ServerConfig config() {
        return config;
    }

    public Set<Connection> connections() {
        return Set.copyOf(connections);
    }

    public List<EventLoop> workers() {
        return List.of(workers);
    }

    public boolean isAccepting() {
        return accepting;
    }

    public void broadcast(Object message) {
        for (Connection connection : connections) {
            connection.write(message);
        }
    }

    public boolean shutdown(Duration grace) throws InterruptedException {
        long deadline = System.nanoTime() + grace.toNanos();
        if (!shutdownStarted.compareAndSet(false, true)) {
            return awaitLoops(deadline);
        }
        accepting = false;
        CompletableFuture<Void> stopped = new CompletableFuture<>();
        try {
            acceptLoop.execute(() -> {
                closeServerChannel();
                stopped.complete(null);
            });
            stopped.get(Math.max(1, remaining(deadline)), TimeUnit.NANOSECONDS);
        } catch (RejectedExecutionException | ExecutionException | TimeoutException e) {
            closeServerChannel();
        }
        for (Connection connection : connections) {
            connection.closeGracefully();
        }
        while (!connections.isEmpty() && remaining(deadline) > 0) {
            Thread.sleep(5);
        }
        boolean drained = connections.isEmpty();
        for (Connection connection : connections) {
            connection.close();
        }
        acceptLoop.shutdown();
        for (EventLoop worker : workers) {
            worker.shutdown();
        }
        long forceDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        return awaitLoops(Math.max(deadline, forceDeadline)) && drained;
    }

    private boolean awaitLoops(long deadline) throws InterruptedException {
        boolean all = acceptLoop.awaitTermination(Duration.ofNanos(Math.max(0, remaining(deadline))));
        for (EventLoop worker : workers) {
            all &= worker.awaitTermination(Duration.ofNanos(Math.max(0, remaining(deadline))));
        }
        return all;
    }

    private static long remaining(long deadline) {
        return deadline - System.nanoTime();
    }

    private void closeServerChannel() {
        try {
            serverChannel.close();
        } catch (IOException ignored) {
            accepting = false;
        }
    }

    private void accept(SocketChannel channel) {
        stats.onAccepted();
        if (!accepting || connections.size() >= config.maxConnections()) {
            stats.onRejected();
            closeQuietly(channel);
            return;
        }
        EventLoop worker = workers[Math.floorMod(nextWorker.getAndIncrement(), workers.length)];
        Connection connection;
        try {
            channel.configureBlocking(false);
            channel.setOption(StandardSocketOptions.TCP_NODELAY, true);
            if (config.socketSendBuffer() > 0) {
                channel.setOption(StandardSocketOptions.SO_SNDBUF, config.socketSendBuffer());
            }
            connection = new Connection(channel, worker, config.pipelineFactory().get(),
                config.handlerFactory().get(), config, pool, stats, connections::remove);
        } catch (IOException | RuntimeException e) {
            stats.onRejected();
            closeQuietly(channel);
            return;
        }
        connections.add(connection);
        try {
            worker.execute(connection::open);
        } catch (RejectedExecutionException e) {
            connections.remove(connection);
            stats.onRejected();
            closeQuietly(channel);
        }
    }

    private static void closeQuietly(SocketChannel channel) {
        try {
            channel.close();
        } catch (IOException ignored) {
            Thread.onSpinWait();
        }
    }

    private final class Acceptor implements IoHandler {
        @Override
        public void onReady(SelectionKey key) throws IOException {
            SocketChannel channel;
            while ((channel = serverChannel.accept()) != null) {
                accept(channel);
            }
        }

        @Override
        public void onFailure(Exception failure) {
            if (!serverChannel.isOpen()) {
                accepting = false;
            }
        }

        @Override
        public void onLoopClosed() {
            closeServerChannel();
        }
    }
}
