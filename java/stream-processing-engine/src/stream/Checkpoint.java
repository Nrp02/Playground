package stream;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.zip.CRC32;

public final class Checkpoint {
    private static final int MAGIC = 0x53504b31;
    private static final int HEADER_BYTES = 4 + 8 + 8 + 4;
    private static final int TRAILER_BYTES = 8;

    private final long id;
    private final long sourceOffset;
    private final byte[] payload;

    public Checkpoint(long id, long sourceOffset, byte[] payload) {
        if (id < 0 || sourceOffset < 0) {
            throw new IllegalArgumentException("checkpoint id and offset must not be negative");
        }
        this.id = id;
        this.sourceOffset = sourceOffset;
        this.payload = payload.clone();
    }

    public long id() {
        return id;
    }

    public long sourceOffset() {
        return sourceOffset;
    }

    public byte[] payload() {
        return payload.clone();
    }

    public DataInputStream payloadStream() {
        return new DataInputStream(new ByteArrayInputStream(payload));
    }

    public byte[] toBytes() {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream(HEADER_BYTES + payload.length + TRAILER_BYTES);
        try (DataOutputStream out = new DataOutputStream(buffer)) {
            out.writeInt(MAGIC);
            out.writeLong(id);
            out.writeLong(sourceOffset);
            out.writeInt(payload.length);
            out.write(payload);
            out.flush();
            CRC32 crc = new CRC32();
            crc.update(buffer.toByteArray());
            out.writeLong(crc.getValue());
        } catch (IOException e) {
            throw new CheckpointException("failed to encode checkpoint " + id, e);
        }
        return buffer.toByteArray();
    }

    public static Checkpoint fromBytes(byte[] bytes) {
        if (bytes.length < HEADER_BYTES + TRAILER_BYTES) {
            throw new CheckpointException("checkpoint truncated at " + bytes.length + " bytes");
        }
        CRC32 crc = new CRC32();
        crc.update(bytes, 0, bytes.length - TRAILER_BYTES);
        try (DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes))) {
            if (in.readInt() != MAGIC) {
                throw new CheckpointException("bad checkpoint magic");
            }
            long id = in.readLong();
            long offset = in.readLong();
            int length = in.readInt();
            if (length < 0 || length != bytes.length - HEADER_BYTES - TRAILER_BYTES) {
                throw new CheckpointException("checkpoint payload length " + length + " does not match size");
            }
            byte[] payload = in.readNBytes(length);
            long stored = in.readLong();
            if (stored != crc.getValue()) {
                throw new CheckpointException("checkpoint " + id + " failed checksum");
            }
            return new Checkpoint(id, offset, payload);
        } catch (IOException e) {
            throw new CheckpointException("failed to decode checkpoint", e);
        }
    }
}
