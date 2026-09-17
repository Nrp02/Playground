import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import nio.Connection;
import nio.ConnectionTimeoutException;
import nio.Handler;
import nio.Server;
import nio.ServerConfig;

public final class BackpressureTest {
    private static final int CHUNK = 64 * 1024;
    private static final int BUDGET = 64;

    private BackpressureTest() {
    }

    public static void run() {
        Assertions.suite("backpressure");

        Assertions.test("slow reader flips writability off then back on", () -> {
            List<Boolean> transitions = new CopyOnWriteArrayList<>();
            AtomicLong pendingAtStop = new AtomicLong();
            AtomicReference<Boolean> pausedAtStop = new AtomicReference<>();
            CountDownLatch filled = new CountDownLatch(1);
            AtomicInteger sent = new AtomicInteger();
            ServerConfig config = filling(sent, new Handler() {
                @Override
                public void onOpen(Connection connection) {
                    pendingAtStop.set(connection.pendingBytes());
                    pausedAtStop.set(connection.isReadPaused());
                    filled.countDown();
                }

                @Override
                public void onMessage(Connection connection, Object message) {
                }

                @Override
                public void onWritabilityChanged(Connection connection, boolean writable) {
                    transitions.add(writable);
                }
            }).build();
            ServerTest.withServer(config, server -> {
                try (TestClient client = new TestClient(server.port(), 16 * 1024)) {
                    Assertions.assertTrue("server filled its queue", filled.await(5, TimeUnit.SECONDS));
                    Assertions.assertTrue("pending above high water mark", pendingAtStop.get() > 256 * 1024);
                    Assertions.assertTrue("reads paused", pausedAtStop.get());
                    Thread.sleep(200);
                    Assertions.assertTrue("budget not yet sent", sent.get() < BUDGET);
                    for (int i = 0; i < BUDGET; i++) {
                        byte[] got = client.readExactly(CHUNK);
                        Assertions.assertTrue("chunk " + i + " intact", got[0] == (byte) i && got[CHUNK - 1] == (byte) i);
                    }
                    Assertions.eventually("ends writable", () -> !transitions.isEmpty()
                        && transitions.get(transitions.size() - 1));
                    for (int i = 0; i < transitions.size(); i++) {
                        Assertions.assertEquals("alternating transition " + i, i % 2 == 1, transitions.get(i));
                    }
                    Assertions.assertEquals("backpressure stat", (long) (transitions.size() / 2),
                        server.stats().backpressureEvents());
                }
            });
        });

        Assertions.test("reads pause while unwritable and resume after the queue drains", () -> {
            AtomicInteger sent = new AtomicInteger();
            ServerConfig config = filling(sent, (connection, message) ->
                connection.write(ByteBuffer.wrap("ACK!".getBytes(StandardCharsets.US_ASCII)))).build();
            ServerTest.withServer(config, server -> {
                try (TestClient client = new TestClient(server.port(), 16 * 1024)) {
                    Assertions.eventually("connection registered", () -> server.connections().size() == 1);
                    Connection connection = server.connections().iterator().next();
                    Thread.sleep(200);
                    Assertions.assertTrue("paused", connection.isReadPaused());
                    client.sendText("hello");
                    Thread.sleep(150);
                    Assertions.assertEquals("request not processed while paused", 0L, server.stats().messagesIn());
                    for (int i = 0; i < BUDGET; i++) {
                        client.readExactly(CHUNK);
                    }
                    Assertions.assertEquals("ack after resume", "ACK!",
                        new String(client.readExactly(4), StandardCharsets.US_ASCII));
                    Assertions.assertEquals("request processed", 1L, server.stats().messagesIn());
                    Assertions.assertFalse("no longer paused", connection.isReadPaused());
                }
            });
        });

        Assertions.test("write stall timeout closes a peer that never reads", () -> {
            AtomicReference<Throwable> cause = new AtomicReference<>();
            CountDownLatch closed = new CountDownLatch(1);
            AtomicInteger sent = new AtomicInteger();
            ServerConfig config = filling(sent, new Handler() {
                @Override
                public void onMessage(Connection connection, Object message) {
                }

                @Override
                public void onClose(Connection connection, Throwable failure) {
                    cause.set(failure);
                    closed.countDown();
                }
            }).writeStallTimeout(Duration.ofMillis(200)).build();
            ServerTest.withServer(config, server -> {
                try (TestClient client = new TestClient(server.port(), 16 * 1024)) {
                    Assertions.assertTrue("closed by stall", closed.await(5, TimeUnit.SECONDS));
                    Assertions.assertTrue("stall cause", cause.get() instanceof ConnectionTimeoutException);
                    Assertions.assertEquals("stat", 1L, server.stats().writeStalls());
                    Assertions.assertTrue("client sees the connection torn down", client.awaitEof());
                }
            });
        });
    }

    private static ServerConfig.Builder filling(AtomicInteger sent, Handler delegate) {
        return ServerConfig.builder(() -> new Handler() {
            private void fill(Connection connection) {
                while (connection.isWritable() && sent.get() < BUDGET) {
                    connection.write(pattern(sent.getAndIncrement()));
                }
            }

            @Override
            public void onOpen(Connection connection) {
                fill(connection);
                delegate.onOpen(connection);
            }

            @Override
            public void onMessage(Connection connection, Object message) {
                delegate.onMessage(connection, message);
            }

            @Override
            public void onWritabilityChanged(Connection connection, boolean writable) {
                delegate.onWritabilityChanged(connection, writable);
                if (writable) {
                    fill(connection);
                }
            }

            @Override
            public void onClose(Connection connection, Throwable cause) {
                delegate.onClose(connection, cause);
            }
        }).waterMarks(64 * 1024, 256 * 1024).socketSendBuffer(32 * 1024);
    }

    private static ByteBuffer pattern(int index) {
        ByteBuffer buffer = ByteBuffer.allocate(CHUNK);
        byte value = (byte) index;
        while (buffer.hasRemaining()) {
            buffer.put(value);
        }
        return buffer.flip();
    }
}
