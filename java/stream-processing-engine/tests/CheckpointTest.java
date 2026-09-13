import java.util.Arrays;
import java.util.List;
import stream.Checkpoint;
import stream.CheckpointException;
import stream.CheckpointStore;
import stream.TransactionalSink;

public final class CheckpointTest {

    private CheckpointTest() {
    }

    public static void run() {
        Assertions.suite("Checkpoint");

        Assertions.test("encodes and decodes id, offset and payload", () -> {
            Checkpoint checkpoint = new Checkpoint(7, 1200, new byte[] {1, 2, 3, 4});
            Checkpoint decoded = Checkpoint.fromBytes(checkpoint.toBytes());
            Assertions.assertEquals("id", 7L, decoded.id());
            Assertions.assertEquals("offset", 1200L, decoded.sourceOffset());
            Assertions.assertTrue("payload", Arrays.equals(new byte[] {1, 2, 3, 4}, decoded.payload()));
        });

        Assertions.test("payload is defensively copied", () -> {
            byte[] payload = {9, 9};
            Checkpoint checkpoint = new Checkpoint(1, 0, payload);
            payload[0] = 0;
            checkpoint.payload()[1] = 0;
            Assertions.assertTrue("unchanged", Arrays.equals(new byte[] {9, 9}, checkpoint.payload()));
        });

        Assertions.test("every single-byte corruption is detected", () -> {
            byte[] bytes = new Checkpoint(3, 40, new byte[] {5, 6, 7}).toBytes();
            for (int i = 0; i < bytes.length; i++) {
                byte[] copy = bytes.clone();
                copy[i] ^= 0x01;
                int position = i;
                Assertions.assertThrows("byte " + position, CheckpointException.class,
                    () -> Checkpoint.fromBytes(copy));
            }
        });

        Assertions.test("truncated checkpoints are rejected", () -> {
            byte[] bytes = new Checkpoint(3, 40, new byte[] {5, 6, 7}).toBytes();
            Assertions.assertThrows("short", CheckpointException.class,
                () -> Checkpoint.fromBytes(Arrays.copyOf(bytes, bytes.length - 1)));
            Assertions.assertThrows("tiny", CheckpointException.class, () -> Checkpoint.fromBytes(new byte[3]));
        });

        Assertions.test("store retains only the newest checkpoints", () -> {
            CheckpointStore store = new CheckpointStore(2);
            store.save(new Checkpoint(0, 0, new byte[0]));
            store.save(new Checkpoint(1, 10, new byte[0]));
            store.save(new Checkpoint(2, 20, new byte[0]));
            Assertions.assertEquals("ids", List.of(1L, 2L), store.ids());
            Assertions.assertEquals("latest", 20L, store.latest().sourceOffset());
            Assertions.assertThrows("evicted", CheckpointException.class, () -> store.get(0));
            Assertions.assertThrows("not newer", CheckpointException.class,
                () -> store.save(new Checkpoint(2, 30, new byte[0])));
        });

        Assertions.test("store surfaces corruption on read", () -> {
            CheckpointStore store = new CheckpointStore(1);
            store.save(new Checkpoint(4, 0, new byte[] {1}));
            store.corrupt(4, 13);
            Assertions.assertThrows("corrupt", CheckpointException.class, store::latest);
            Assertions.assertThrows("empty", CheckpointException.class, () -> new CheckpointStore(1).latest());
        });

        Assertions.test("transactional sink exposes only committed writes", () -> {
            TransactionalSink<String> sink = new TransactionalSink<>();
            sink.write("a");
            sink.commit();
            sink.writeAll(List.of("b", "c"));
            Assertions.assertEquals("pending hidden", List.of("a"), sink.committed());
            sink.rollback();
            sink.write("d");
            sink.commit();
            Assertions.assertEquals("rolled back", List.of("a", "d"), sink.committed());
            Assertions.assertEquals("discarded", 2L, sink.discardedCount());
            Assertions.assertEquals("commits", 2, sink.commitCount());
            Assertions.assertEquals("rollbacks", 1, sink.rollbackCount());
        });
    }
}
