package stream;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.Collections;
import java.util.SortedSet;
import java.util.TreeSet;

public final class Codecs {

    public static final Codec<Long> LONG = new Codec<>() {
        @Override
        public void write(DataOutputStream out, Long value) throws IOException {
            out.writeLong(value);
        }

        @Override
        public Long read(DataInputStream in) throws IOException {
            return in.readLong();
        }
    };

    public static final Codec<Mean> MEAN = new Codec<>() {
        @Override
        public void write(DataOutputStream out, Mean value) throws IOException {
            out.writeLong(value.sum());
            out.writeLong(value.count());
        }

        @Override
        public Mean read(DataInputStream in) throws IOException {
            long sum = in.readLong();
            long count = in.readLong();
            return new Mean(sum, count);
        }
    };

    public static final Codec<SortedSet<String>> STRING_SET = new Codec<>() {
        @Override
        public void write(DataOutputStream out, SortedSet<String> value) throws IOException {
            out.writeInt(value.size());
            for (String element : value) {
                out.writeUTF(element);
            }
        }

        @Override
        public SortedSet<String> read(DataInputStream in) throws IOException {
            int size = in.readInt();
            if (size < 0) {
                throw new IOException("negative set size " + size);
            }
            TreeSet<String> set = new TreeSet<>();
            for (int i = 0; i < size; i++) {
                set.add(in.readUTF());
            }
            return Collections.unmodifiableSortedSet(set);
        }
    };

    private Codecs() {
    }
}
