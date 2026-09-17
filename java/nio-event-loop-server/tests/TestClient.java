import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;

public final class TestClient implements AutoCloseable {
    private final Socket socket;
    private final DataInputStream in;
    private final DataOutputStream out;

    public TestClient(int port) throws IOException {
        this(port, 0);
    }

    public TestClient(int port, int receiveBuffer) throws IOException {
        socket = new Socket();
        if (receiveBuffer > 0) {
            socket.setReceiveBufferSize(receiveBuffer);
        }
        socket.connect(new InetSocketAddress("127.0.0.1", port), 3000);
        socket.setSoTimeout(5000);
        in = new DataInputStream(socket.getInputStream());
        out = new DataOutputStream(socket.getOutputStream());
    }

    public void sendRaw(byte[] bytes) throws IOException {
        out.write(bytes);
        out.flush();
    }

    public void sendText(String text) throws IOException {
        sendRaw(text.getBytes(StandardCharsets.UTF_8));
    }

    public void sendFrame(String text) throws IOException {
        byte[] payload = text.getBytes(StandardCharsets.UTF_8);
        out.writeInt(payload.length);
        out.write(payload);
        out.flush();
    }

    public String readLine() throws IOException {
        ByteArrayOutputStream line = new ByteArrayOutputStream();
        int b;
        while ((b = in.read()) != '\n') {
            if (b == -1) {
                throw new IOException("stream ended mid-line: " + line.toString(StandardCharsets.UTF_8));
            }
            line.write(b);
        }
        return line.toString(StandardCharsets.UTF_8);
    }

    public String readFrame() throws IOException {
        byte[] payload = new byte[in.readInt()];
        in.readFully(payload);
        return new String(payload, StandardCharsets.UTF_8);
    }

    public byte[] readExactly(int length) throws IOException {
        byte[] data = new byte[length];
        in.readFully(data);
        return data;
    }

    public boolean awaitEof() throws IOException {
        try {
            while (true) {
                int b = in.read();
                if (b == -1) {
                    return true;
                }
            }
        } catch (SocketTimeoutException e) {
            return false;
        } catch (IOException e) {
            return true;
        }
    }

    public void shutdownOutput() throws IOException {
        socket.shutdownOutput();
    }

    @Override
    public void close() throws IOException {
        socket.close();
    }
}
