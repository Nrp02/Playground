import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import nio.Connection;
import nio.ConnectionTimeoutException;
import nio.Handler;
import nio.LengthPrefixedFrameCodec;
import nio.LineFrameCodec;
import nio.Pipeline;
import nio.ProtocolException;
import nio.Server;
import nio.ServerConfig;
import nio.Utf8StringCodec;

public final class ServerTest {

    private ServerTest() {
    }

    @FunctionalInterface
    interface ServerBody {
        void run(Server server) throws Exception;
    }

    public static void run() {
        Assertions.suite("server");

        Assertions.test("echoes lines for many concurrent clients", () -> {
            ServerConfig config = lines(() -> (connection, message) -> connection.write("echo " + message))
                .workerThreads(3)
                .build();
            withServer(config, server -> {
                ExecutorService pool = Executors.newFixedThreadPool(16);
                try {
                    List<Future<Boolean>> results = new ArrayList<>();
                    for (int c = 0; c < 32; c++) {
                        int clientId = c;
                        results.add(pool.submit(() -> {
                            try (TestClient client = new TestClient(server.port())) {
                                for (int i = 0; i < 50; i++) {
                                    client.sendText("c" + clientId + "-" + i + "\n");
                                }
                                for (int i = 0; i < 50; i++) {
                                    if (!client.readLine().equals("echo c" + clientId + "-" + i)) {
                                        return false;
                                    }
                                }
                                return true;
                            }
                        }));
                    }
                    for (Future<Boolean> result : results) {
                        Assertions.assertTrue("client received ordered echoes", result.get(10, TimeUnit.SECONDS));
                    }
                } finally {
                    pool.shutdownNow();
                }
                Assertions.assertEquals("messages in", 1600L, server.stats().messagesIn());
                Assertions.eventually("all connections released", () -> server.connections().isEmpty());
            });
        });

        Assertions.test("connections are spread across worker loops", () -> {
            Set<String> loops = ConcurrentHashMap.newKeySet();
            ServerConfig config = lines(() -> new Handler() {
                @Override
                public void onOpen(Connection connection) {
                    loops.add(Thread.currentThread().getName());
                    connection.write("hi");
                }

                @Override
                public void onMessage(Connection connection, Object message) {
                }
            }).workerThreads(3).build();
            withServer(config, server -> {
                List<TestClient> clients = new ArrayList<>();
                try {
                    for (int i = 0; i < 6; i++) {
                        TestClient client = new TestClient(server.port());
                        clients.add(client);
                        Assertions.assertEquals("greeting", "hi", client.readLine());
                    }
                    Assertions.assertEquals("all three workers used", 3, loops.size());
                } finally {
                    for (TestClient client : clients) {
                        client.close();
                    }
                }
            });
        });

        Assertions.test("length-prefixed request/response with pipelined frames", () -> {
            ServerConfig config = ServerConfig.builder(() -> (connection, message) ->
                    connection.write(new StringBuilder((String) message).reverse().toString()))
                .pipeline(() -> Pipeline.of(new LengthPrefixedFrameCodec(1 << 20), new Utf8StringCodec()))
                .build();
            withServer(config, server -> {
                try (TestClient client = new TestClient(server.port())) {
                    String big = "x".repeat(200_000) + "END";
                    client.sendFrame("abc");
                    client.sendFrame(big);
                    client.sendFrame("ünïcode");
                    Assertions.assertEquals("small", "cba", client.readFrame());
                    Assertions.assertEquals("big", new StringBuilder(big).reverse().toString(), client.readFrame());
                    Assertions.assertEquals("unicode", "edocïnü", client.readFrame());
                }
            });
        });

        Assertions.test("protocol errors close the connection with the cause", () -> {
            AtomicReference<Throwable> cause = new AtomicReference<>();
            CountDownLatch closed = new CountDownLatch(1);
            ServerConfig config = ServerConfig.builder(() -> new Handler() {
                @Override
                public void onMessage(Connection connection, Object message) {
                }

                @Override
                public void onClose(Connection connection, Throwable failure) {
                    cause.set(failure);
                    closed.countDown();
                }
            }).pipeline(() -> Pipeline.of(new LineFrameCodec(8), new Utf8StringCodec())).build();
            withServer(config, server -> {
                try (TestClient client = new TestClient(server.port())) {
                    client.sendText("this line is far too long\n");
                    Assertions.assertTrue("peer sees EOF", client.awaitEof());
                }
                Assertions.assertTrue("onClose called", closed.await(3, TimeUnit.SECONDS));
                Assertions.assertTrue("cause is protocol error", cause.get() instanceof ProtocolException);
                Assertions.assertEquals("stat", 1L, server.stats().protocolErrors());
            });
        });

        Assertions.test("handler exceptions close only the offending connection", () -> {
            ServerConfig config = lines(() -> (connection, message) -> {
                if (message.equals("crash")) {
                    throw new IllegalStateException("handler bug");
                }
                connection.write("ok " + message);
            }).build();
            withServer(config, server -> {
                try (TestClient bad = new TestClient(server.port());
                     TestClient good = new TestClient(server.port())) {
                    bad.sendText("crash\n");
                    Assertions.assertTrue("bad closed", bad.awaitEof());
                    good.sendText("still\n");
                    Assertions.assertEquals("good served", "ok still", good.readLine());
                }
            });
        });

        Assertions.test("read idle timeout disconnects silent clients but not chatty ones", () -> {
            AtomicReference<Throwable> cause = new AtomicReference<>();
            ServerConfig config = lines(() -> new Handler() {
                @Override
                public void onMessage(Connection connection, Object message) {
                    connection.write("pong");
                }

                @Override
                public void onClose(Connection connection, Throwable failure) {
                    if (failure != null) {
                        cause.set(failure);
                    }
                }
            }).readIdleTimeout(Duration.ofMillis(200)).build();
            withServer(config, server -> {
                try (TestClient silent = new TestClient(server.port());
                     TestClient chatty = new TestClient(server.port())) {
                    for (int i = 0; i < 8; i++) {
                        chatty.sendText("ping\n");
                        Assertions.assertEquals("pong " + i, "pong", chatty.readLine());
                        Thread.sleep(60);
                    }
                    Assertions.assertTrue("silent client closed", silent.awaitEof());
                    chatty.sendText("ping\n");
                    Assertions.assertEquals("chatty still open", "pong", chatty.readLine());
                }
                Assertions.assertTrue("timeout cause", cause.get() instanceof ConnectionTimeoutException);
                Assertions.assertTrue("stat", server.stats().idleTimeouts() >= 1);
            });
        });

        Assertions.test("max connections rejects extra clients", () -> {
            ServerConfig config = lines(() -> new Handler() {
                @Override
                public void onOpen(Connection connection) {
                    connection.write("welcome");
                }

                @Override
                public void onMessage(Connection connection, Object message) {
                }
            }).maxConnections(2).build();
            withServer(config, server -> {
                try (TestClient a = new TestClient(server.port());
                     TestClient b = new TestClient(server.port())) {
                    Assertions.assertEquals("a", "welcome", a.readLine());
                    Assertions.assertEquals("b", "welcome", b.readLine());
                    try (TestClient c = new TestClient(server.port())) {
                        Assertions.assertTrue("c rejected", c.awaitEof());
                    }
                    Assertions.assertEquals("rejected stat", 1L, server.stats().rejected());
                }
                Assertions.eventually("slots freed", () -> server.connections().isEmpty());
                try (TestClient d = new TestClient(server.port())) {
                    Assertions.assertEquals("d admitted after release", "welcome", d.readLine());
                }
            });
        });

        Assertions.test("writes from foreign threads are delivered in order", () -> {
            CountDownLatch opened = new CountDownLatch(1);
            AtomicReference<Connection> ref = new AtomicReference<>();
            ServerConfig config = lines(() -> new Handler() {
                @Override
                public void onOpen(Connection connection) {
                    ref.set(connection);
                    opened.countDown();
                }

                @Override
                public void onMessage(Connection connection, Object message) {
                }
            }).build();
            withServer(config, server -> {
                try (TestClient client = new TestClient(server.port())) {
                    Assertions.assertTrue("opened", opened.await(3, TimeUnit.SECONDS));
                    Connection connection = ref.get();
                    Assertions.assertFalse("test thread is not the loop", connection.loop().inEventLoop());
                    for (int i = 0; i < 1000; i++) {
                        connection.write("line " + i);
                    }
                    for (int i = 0; i < 1000; i++) {
                        Assertions.assertEquals("line " + i, "line " + i, client.readLine());
                    }
                    server.broadcast("everyone");
                    Assertions.assertEquals("broadcast", "everyone", client.readLine());
                }
            });
        });

        Assertions.test("writeAndClose flushes before closing and drops later writes", () -> {
            ServerConfig config = lines(() -> (connection, message) -> {
                connection.writeAndClose("x".repeat(100_000));
                connection.write("dropped");
            }).pipeline(() -> Pipeline.of(new LineFrameCodec(1 << 20), new Utf8StringCodec())).build();
            withServer(config, server -> {
                try (TestClient client = new TestClient(server.port())) {
                    client.sendText("go\n");
                    Assertions.assertEquals("full payload", 100_000, client.readLine().length());
                    Assertions.assertTrue("closed afterwards", client.awaitEof());
                }
                Assertions.assertEquals("dropped stat", 1L, server.stats().droppedWrites());
            });
        });

        Assertions.test("client half-close still receives pending replies", () -> {
            ServerConfig config = lines(() -> (connection, message) -> connection.write("got " + message)).build();
            withServer(config, server -> {
                try (TestClient client = new TestClient(server.port())) {
                    client.sendText("a\nb\n");
                    client.shutdownOutput();
                    Assertions.assertEquals("a", "got a", client.readLine());
                    Assertions.assertEquals("b", "got b", client.readLine());
                    Assertions.assertTrue("server closes after EOF", client.awaitEof());
                }
            });
        });

        Assertions.test("attributes are per connection", () -> {
            ServerConfig config = lines(() -> (connection, message) -> {
                Object count = connection.attribute("count");
                int next = count == null ? 1 : (Integer) count + 1;
                connection.attribute("count", next);
                connection.write(String.valueOf(next));
            }).build();
            withServer(config, server -> {
                try (TestClient a = new TestClient(server.port());
                     TestClient b = new TestClient(server.port())) {
                    a.sendText("x\nx\nx\n");
                    b.sendText("x\n");
                    Assertions.assertEquals("a1", "1", a.readLine());
                    Assertions.assertEquals("a2", "2", a.readLine());
                    Assertions.assertEquals("a3", "3", a.readLine());
                    Assertions.assertEquals("b1", "1", b.readLine());
                }
            });
        });

        Assertions.test("read buffers are returned to the pool", () -> {
            ServerConfig config = lines(() -> (connection, message) -> connection.write(message))
                .readBufferSize(64)
                .build();
            withServer(config, server -> {
                try (TestClient client = new TestClient(server.port())) {
                    for (int i = 0; i < 20; i++) {
                        client.sendText("message-" + i + "-" + "y".repeat(100) + "\n");
                        Assertions.assertTrue("echo", client.readLine().startsWith("message-" + i));
                    }
                }
                Assertions.eventually("no leases", () -> server.bufferPool().leasedCount() == 0);
                Assertions.assertTrue("buffers reused", server.bufferPool().reuses() > server.bufferPool().allocations());
            });
        });

        Assertions.test("graceful shutdown closes clients and stops accepting", () -> {
            List<Connection> closed = new CopyOnWriteArrayList<>();
            ServerConfig config = lines(() -> new Handler() {
                @Override
                public void onOpen(Connection connection) {
                    connection.write("ready");
                }

                @Override
                public void onMessage(Connection connection, Object message) {
                }

                @Override
                public void onClose(Connection connection, Throwable cause) {
                    closed.add(connection);
                }
            }).build();
            Server server = Server.start(config);
            int port = server.port();
            try (TestClient a = new TestClient(port);
                 TestClient b = new TestClient(port)) {
                Assertions.assertEquals("a ready", "ready", a.readLine());
                Assertions.assertEquals("b ready", "ready", b.readLine());
                Assertions.assertTrue("clean shutdown", server.shutdown(Duration.ofSeconds(3)));
                Assertions.assertTrue("a closed", a.awaitEof());
                Assertions.assertTrue("b closed", b.awaitEof());
            }
            Assertions.assertEquals("onClose for both", 2, closed.size());
            Assertions.assertFalse("not accepting", server.isAccepting());
            Assertions.assertTrue("loops terminated", server.workers().stream().allMatch(l -> l.isTerminated()));
            Assertions.assertThrows("port closed", java.io.IOException.class, () -> new TestClient(port).close());
            Assertions.assertTrue("second shutdown is harmless", server.shutdown(Duration.ofSeconds(1)));
        });

        Assertions.test("config validation", () -> {
            Supplier<Handler> handler = () -> (connection, message) -> { };
            Assertions.assertThrows("watermarks", IllegalArgumentException.class,
                () -> ServerConfig.builder(handler).waterMarks(10, 5).build());
            Assertions.assertThrows("workers", IllegalArgumentException.class,
                () -> ServerConfig.builder(handler).workerThreads(0).build());
            Assertions.assertThrows("timeout", IllegalArgumentException.class,
                () -> ServerConfig.builder(handler).readIdleTimeout(Duration.ZERO).build());
            Assertions.assertThrows("port", IllegalArgumentException.class,
                () -> ServerConfig.builder(handler).port(70000).build());
        });
    }

    private static ServerConfig.Builder lines(Supplier<Handler> handler) {
        return ServerConfig.builder(handler)
            .pipeline(() -> Pipeline.of(new LineFrameCodec(4096), new Utf8StringCodec()));
    }

    static void withServer(ServerConfig config, ServerBody body) throws Exception {
        Server server = Server.start(config);
        try {
            body.run(server);
        } finally {
            if (!server.shutdown(Duration.ofSeconds(3))) {
                throw new AssertionError("server did not shut down cleanly");
            }
        }
    }
}
