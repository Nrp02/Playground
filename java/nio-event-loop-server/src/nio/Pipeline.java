package nio;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public final class Pipeline {
    private final List<Codec> stages;

    private Pipeline(List<Codec> stages) {
        this.stages = List.copyOf(stages);
    }

    public static Pipeline of(Codec... stages) {
        return new Pipeline(List.of(stages));
    }

    public List<Object> inbound(ByteBuffer bytes) throws ProtocolException {
        List<Object> current = new ArrayList<>(1);
        if (stages.isEmpty()) {
            byte[] copy = new byte[bytes.remaining()];
            bytes.get(copy);
            current.add(copy);
            return current;
        }
        current.add(bytes);
        for (Codec stage : stages) {
            List<Object> next = new ArrayList<>(current.size());
            for (Object message : current) {
                stage.decode(message, next);
            }
            current = next;
            if (current.isEmpty()) {
                break;
            }
        }
        return current;
    }

    public ByteBuffer outbound(Object message) throws ProtocolException {
        Object current = message;
        for (int i = stages.size() - 1; i >= 0; i--) {
            current = stages.get(i).encode(current);
        }
        if (current instanceof ByteBuffer buffer) {
            return buffer;
        }
        if (current instanceof byte[] bytes) {
            return ByteBuffer.wrap(bytes);
        }
        throw Codecs.unsupported("Pipeline tail", current);
    }

    public int size() {
        return stages.size();
    }
}
