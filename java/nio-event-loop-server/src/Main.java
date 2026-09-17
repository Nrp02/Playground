import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import nio.Connection;
import nio.Handler;
import nio.LengthPrefixedFrameCodec;
import nio.LineFrameCodec;
import nio.Pipeline;
import nio.Server;
import nio.ServerConfig;
import nio.Utf8StringCodec;

public final class Main {

    private Main() {
    }

    public static void main(String[] args) throws Exception {
        lineProtocolDemo();
        System.out.println();
        keyValueDemo();
        System.out.println();
        idleTimeoutDemo();
    }

    private static void lineProtocolDemo() throws Exception {
        System.out.println("== line protocol: chat room over a UTF-8 line codec");
        Map<Long, String> names = new ConcurrentHashMap<>();
        Server[] holder = new Server[1];
        Handler chat = new Handler() {
            @Override
            public void onOpen(Connection connection) {
                connection.write("welcome, send NAME <nick> then messages");
            }

            @Override
            public void onMessage(Connection connection, Object message) {
                String line = (String) message;
                if (line.startsWith("NAME ")) {
                    names.put(connection.id(), line.substring(5));
                    connection.write("ok " + line.substring(5));
                } else if (line.equals("QUIT")) {
                    connection.writeAndClose("bye");
                } else {
                    String from = names.getOrDefault(connection.id(), "anon");
                    holder[0].broadcast(from + ": " + line);
                }
            }

            @Override
            public void onClose(Connection connection, Throwable cause) {
                names.remove(connection.id());
            }
        };
        ServerConfig config = ServerConfig.builder(() -> chat)
            .workerThreads(2)
            .pipeline(() -> Pipeline.of(new LineFrameCodec(1024), new Utf8StringCodec()))
            .build();
        Server server = Server.start(config);
        holder[0] = server;
        System.out.println("listening on 127.0.0.1:" + server.port());

        try (Socket alice = new Socket("127.0.0.1", server.port());
             Socket bob = new Socket("127.0.0.1", server.port())) {
            alice.setSoTimeout(3000);
            bob.setSoTimeout(3000);
            System.out.println("alice <- " + readLine(alice.getInputStream()));
            System.out.println("bob   <- " + readLine(bob.getInputStream()));
            send(alice, "NAME alice\r\n");
            send(bob, "NAME bob\n");
            System.out.println("alice <- " + readLine(alice.getInputStream()));
            System.out.println("bob   <- " + readLine(bob.getInputStream()));
            send(alice, "hello ");
            Thread.sleep(20);
            send(alice, "everyone\nhow are you?\n");
            for (int i = 0; i < 2; i++) {
                System.out.println("bob   <- " + readLine(bob.getInputStream()));
                System.out.println("alice <- " + readLine(alice.getInputStream()));
            }
            send(bob, "QUIT\n");
            System.out.println("bob   <- " + readLine(bob.getInputStream()));
            System.out.println("bob stream ended: " + (bob.getInputStream().read() == -1));
        }
        System.out.println("graceful shutdown: " + server.shutdown(Duration.ofSeconds(2)));
        System.out.println("stats: " + server.stats());
        System.out.println("buffer pool: allocations=" + server.bufferPool().allocations()
            + " reuses=" + server.bufferPool().reuses());
    }

    private static void keyValueDemo() throws Exception {
        System.out.println("== length-prefixed key/value protocol");
        Map<String, String> store = new ConcurrentHashMap<>();
        Handler kv = (connection, message) -> {
            String[] parts = ((String) message).split(" ", 3);
            switch (parts[0]) {
                case "SET" -> {
                    store.put(parts[1], parts[2]);
                    connection.write("OK");
                }
                case "GET" -> connection.write(store.getOrDefault(parts[1], "(nil)"));
                case "DEL" -> connection.write(store.remove(parts[1]) == null ? "0" : "1");
                default -> connection.write("ERR unknown command " + parts[0]);
            }
        };
        Server server = Server.start(ServerConfig.builder(() -> kv)
            .pipeline(() -> Pipeline.of(new LengthPrefixedFrameCodec(64 * 1024), new Utf8StringCodec()))
            .build());
        try (Socket socket = new Socket("127.0.0.1", server.port())) {
            socket.setSoTimeout(3000);
            DataOutputStream out = new DataOutputStream(socket.getOutputStream());
            DataInputStream in = new DataInputStream(socket.getInputStream());
            String[] commands = {"SET lang java", "SET loop nio", "GET lang", "GET missing", "DEL loop", "GET loop", "PING"};
            for (String command : commands) {
                byte[] payload = command.getBytes(StandardCharsets.UTF_8);
                out.writeInt(payload.length);
                out.write(payload);
            }
            out.flush();
            for (String command : commands) {
                byte[] reply = new byte[in.readInt()];
                in.readFully(reply);
                System.out.println(String.format("%-14s -> %s", command, new String(reply, StandardCharsets.UTF_8)));
            }
            out.writeInt(1 << 30);
            out.flush();
            System.out.println("oversized frame closes connection: " + (in.read() == -1));
        }
        server.shutdown(Duration.ofSeconds(2));
        System.out.println("stats: " + server.stats());
    }

    private static void idleTimeoutDemo() throws Exception {
        System.out.println("== idle timeout");
        Server server = Server.start(ServerConfig.builder(() -> (connection, message) -> connection.write(message))
            .readIdleTimeout(Duration.ofMillis(150))
            .build());
        try (Socket socket = new Socket("127.0.0.1", server.port())) {
            socket.setSoTimeout(3000);
            long start = System.nanoTime();
            int result = socket.getInputStream().read();
            long elapsed = (System.nanoTime() - start) / 1_000_000;
            System.out.println("silent client disconnected: " + (result == -1) + " after ~" + elapsed + "ms");
        }
        server.shutdown(Duration.ofSeconds(2));
        System.out.println("idle timeouts: " + server.stats().idleTimeouts());
    }

    private static void send(Socket socket, String text) throws IOException {
        socket.getOutputStream().write(text.getBytes(StandardCharsets.UTF_8));
        socket.getOutputStream().flush();
    }

    private static String readLine(InputStream in) throws IOException {
        StringBuilder line = new StringBuilder();
        int b;
        while ((b = in.read()) != -1 && b != '\n') {
            line.append((char) b);
        }
        return line.toString();
    }
}
