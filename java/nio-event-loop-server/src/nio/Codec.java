package nio;

import java.util.List;

public interface Codec {
    void decode(Object message, List<Object> out) throws ProtocolException;

    Object encode(Object message) throws ProtocolException;
}
