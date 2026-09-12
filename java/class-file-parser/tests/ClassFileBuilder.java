import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public final class ClassFileBuilder {
    private final List<byte[]> pool = new ArrayList<>();
    private final List<byte[]> methods = new ArrayList<>();
    private final List<byte[]> fields = new ArrayList<>();
    private int majorVersion = 61;
    private int minorVersion = 0;
    private int accessFlags = 0x0021;
    private int thisClass;
    private int superClass;
    private long magic = 0xCAFEBABEL;

    public ClassFileBuilder() {
        thisClass = classEntry("Demo");
        superClass = classEntry("java/lang/Object");
    }

    public ClassFileBuilder magic(long value) {
        magic = value;
        return this;
    }

    public ClassFileBuilder majorVersion(int value) {
        majorVersion = value;
        return this;
    }

    public ClassFileBuilder minorVersion(int value) {
        minorVersion = value;
        return this;
    }

    public ClassFileBuilder accessFlags(int value) {
        accessFlags = value;
        return this;
    }

    public ClassFileBuilder thisClassIndex(int value) {
        thisClass = value;
        return this;
    }

    public ClassFileBuilder superClassIndex(int value) {
        superClass = value;
        return this;
    }

    public int utf8(String text) {
        byte[] raw = text.getBytes(StandardCharsets.UTF_8);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(1);
        writeShort(out, raw.length);
        out.writeBytes(raw);
        return addEntry(out.toByteArray(), 1);
    }

    public int classEntry(String internalName) {
        int nameIndex = utf8(internalName);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(7);
        writeShort(out, nameIndex);
        return addEntry(out.toByteArray(), 1);
    }

    public int integerEntry(int value) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(3);
        writeInt(out, value);
        return addEntry(out.toByteArray(), 1);
    }

    public int longEntry(long value) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(5);
        writeInt(out, (int) (value >>> 32));
        writeInt(out, (int) value);
        return addEntry(out.toByteArray(), 2);
    }

    public int stringEntry(String value) {
        int utf8Index = utf8(value);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(8);
        writeShort(out, utf8Index);
        return addEntry(out.toByteArray(), 1);
    }

    public int nameAndType(String name, String descriptor) {
        int nameIndex = utf8(name);
        int descriptorIndex = utf8(descriptor);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(12);
        writeShort(out, nameIndex);
        writeShort(out, descriptorIndex);
        return addEntry(out.toByteArray(), 1);
    }

    public int methodRef(String owner, String name, String descriptor) {
        int classIndex = classEntry(owner);
        int natIndex = nameAndType(name, descriptor);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(10);
        writeShort(out, classIndex);
        writeShort(out, natIndex);
        return addEntry(out.toByteArray(), 1);
    }

    public int fieldRef(String owner, String name, String descriptor) {
        int classIndex = classEntry(owner);
        int natIndex = nameAndType(name, descriptor);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(9);
        writeShort(out, classIndex);
        writeShort(out, natIndex);
        return addEntry(out.toByteArray(), 1);
    }

    public ClassFileBuilder field(int flags, String name, String descriptor) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeShort(out, flags);
        writeShort(out, utf8(name));
        writeShort(out, utf8(descriptor));
        writeShort(out, 0);
        fields.add(out.toByteArray());
        return this;
    }

    public ClassFileBuilder abstractMethod(int flags, String name, String descriptor) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeShort(out, flags);
        writeShort(out, utf8(name));
        writeShort(out, utf8(descriptor));
        writeShort(out, 0);
        methods.add(out.toByteArray());
        return this;
    }

    public ClassFileBuilder method(int flags, String name, String descriptor, int maxStack,
                                   int maxLocals, byte[] code) {
        return method(flags, name, descriptor, maxStack, maxLocals, code, new int[0][]);
    }

    public ClassFileBuilder method(int flags, String name, String descriptor, int maxStack,
                                   int maxLocals, byte[] code, int[][] handlers) {
        int codeNameIndex = utf8("Code");
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        writeShort(body, maxStack);
        writeShort(body, maxLocals);
        writeInt(body, code.length);
        body.writeBytes(code);
        writeShort(body, handlers.length);
        for (int[] handler : handlers) {
            for (int value : handler) {
                writeShort(body, value);
            }
        }
        writeShort(body, 0);

        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeShort(out, flags);
        writeShort(out, utf8(name));
        writeShort(out, utf8(descriptor));
        writeShort(out, 1);
        writeShort(out, codeNameIndex);
        writeInt(out, body.size());
        out.writeBytes(body.toByteArray());
        methods.add(out.toByteArray());
        return this;
    }

    public byte[] build() {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeInt(out, (int) magic);
        writeShort(out, minorVersion);
        writeShort(out, majorVersion);
        writeShort(out, pool.size() + 1);
        for (byte[] entry : pool) {
            if (entry.length > 0) {
                out.writeBytes(entry);
            }
        }
        writeShort(out, accessFlags);
        writeShort(out, thisClass);
        writeShort(out, superClass);
        writeShort(out, 0);
        writeShort(out, fields.size());
        fields.forEach(out::writeBytes);
        writeShort(out, methods.size());
        methods.forEach(out::writeBytes);
        writeShort(out, 0);
        return out.toByteArray();
    }

    private int addEntry(byte[] encoded, int slots) {
        int index = pool.size() + 1;
        pool.add(encoded);
        for (int i = 1; i < slots; i++) {
            pool.add(new byte[0]);
        }
        return index;
    }

    private static void writeShort(ByteArrayOutputStream out, int value) {
        out.write((value >>> 8) & 0xFF);
        out.write(value & 0xFF);
    }

    private static void writeInt(ByteArrayOutputStream out, int value) {
        out.write((value >>> 24) & 0xFF);
        out.write((value >>> 16) & 0xFF);
        out.write((value >>> 8) & 0xFF);
        out.write(value & 0xFF);
    }
}
