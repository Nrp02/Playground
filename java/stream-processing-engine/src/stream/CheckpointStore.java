package stream;

import java.util.List;
import java.util.TreeMap;

public final class CheckpointStore {
    private final int retained;
    private final TreeMap<Long, byte[]> stored = new TreeMap<>();

    public CheckpointStore(int retained) {
        if (retained <= 0) {
            throw new IllegalArgumentException("must retain at least one checkpoint");
        }
        this.retained = retained;
    }

    public void save(Checkpoint checkpoint) {
        if (!stored.isEmpty() && checkpoint.id() <= stored.lastKey()) {
            throw new CheckpointException("checkpoint id " + checkpoint.id() + " is not newer than "
                + stored.lastKey());
        }
        stored.put(checkpoint.id(), checkpoint.toBytes());
        while (stored.size() > retained) {
            stored.pollFirstEntry();
        }
    }

    public Checkpoint latest() {
        if (stored.isEmpty()) {
            throw new CheckpointException("no checkpoint available");
        }
        return Checkpoint.fromBytes(stored.lastEntry().getValue());
    }

    public Checkpoint get(long id) {
        byte[] bytes = stored.get(id);
        if (bytes == null) {
            throw new CheckpointException("checkpoint " + id + " not retained");
        }
        return Checkpoint.fromBytes(bytes);
    }

    public List<Long> ids() {
        return List.copyOf(stored.keySet());
    }

    public int sizeInBytes(long id) {
        byte[] bytes = stored.get(id);
        return bytes == null ? 0 : bytes.length;
    }

    public void corrupt(long id, int position) {
        byte[] bytes = stored.get(id);
        if (bytes == null) {
            throw new CheckpointException("checkpoint " + id + " not retained");
        }
        bytes[Math.floorMod(position, bytes.length)] ^= 0x5a;
    }
}
