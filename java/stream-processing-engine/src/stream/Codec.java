package stream;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;

public interface Codec<V> {

    void write(DataOutputStream out, V value) throws IOException;

    V read(DataInputStream in) throws IOException;
}
