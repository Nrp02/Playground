package classfile;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class ClassFileParser {
    public static final long MAGIC = 0xCAFEBABEL;

    private ClassFileParser() {
    }

    public static ClassFile parse(Path path) throws IOException {
        return parse(Files.readAllBytes(path));
    }

    public static ClassFile parse(InputStream stream) throws IOException {
        return parse(stream.readAllBytes());
    }

    public static ClassFile parse(byte[] data) {
        ByteReader reader = new ByteReader(data);
        long magic = reader.u4();
        if (magic != MAGIC) {
            throw new ClassFormatException(
                String.format("bad magic: expected 0xCAFEBABE but found 0x%08X", magic));
        }
        int minor = reader.u2();
        int major = reader.u2();
        ConstantPool pool = readConstantPool(reader);
        int accessFlags = reader.u2();
        int thisClass = reader.u2();
        int superClass = reader.u2();

        int interfaceCount = reader.u2();
        List<Integer> interfaces = new ArrayList<>(interfaceCount);
        for (int i = 0; i < interfaceCount; i++) {
            interfaces.add(reader.u2());
        }

        List<MemberInfo> fields = readMembers(reader, pool);
        List<MemberInfo> methods = readMembers(reader, pool);
        List<AttributeInfo> attributes = readAttributes(reader, pool);

        if (reader.hasRemaining()) {
            throw new ClassFormatException(reader.remaining() + " trailing byte(s) after class file end");
        }
        return new ClassFile(minor, major, pool, accessFlags, thisClass, superClass,
            List.copyOf(interfaces), fields, methods, attributes);
    }

    private static ConstantPool readConstantPool(ByteReader reader) {
        int count = reader.u2();
        if (count < 1) {
            throw new ClassFormatException("constant_pool_count must be at least 1, found " + count);
        }
        Constant[] entries = new Constant[count];
        entries[0] = new Constant.Unusable();
        int index = 1;
        while (index < count) {
            int tag = reader.u1();
            Constant entry = readConstant(reader, tag);
            entries[index] = entry;
            index++;
            if (entry instanceof Constant.LongValue || entry instanceof Constant.DoubleValue) {
                if (index < count) {
                    entries[index] = new Constant.Unusable();
                }
                index++;
            }
        }
        for (int i = 0; i < count; i++) {
            if (entries[i] == null) {
                entries[i] = new Constant.Unusable();
            }
        }
        return new ConstantPool(entries);
    }

    private static Constant readConstant(ByteReader reader, int tag) {
        return switch (tag) {
            case 1 -> new Constant.Utf8(readModifiedUtf8(reader));
            case 3 -> new Constant.IntegerValue(reader.s4());
            case 4 -> new Constant.FloatValue(Float.intBitsToFloat(reader.s4()));
            case 5 -> new Constant.LongValue(reader.s8());
            case 6 -> new Constant.DoubleValue(Double.longBitsToDouble(reader.s8()));
            case 7 -> new Constant.ClassRef(reader.u2());
            case 8 -> new Constant.StringRef(reader.u2());
            case 9 -> new Constant.FieldRef(reader.u2(), reader.u2());
            case 10 -> new Constant.MethodRef(reader.u2(), reader.u2());
            case 11 -> new Constant.InterfaceMethodRef(reader.u2(), reader.u2());
            case 12 -> new Constant.NameAndType(reader.u2(), reader.u2());
            case 15 -> new Constant.MethodHandle(reader.u1(), reader.u2());
            case 16 -> new Constant.MethodType(reader.u2());
            case 17 -> new Constant.DynamicValue(reader.u2(), reader.u2());
            case 18 -> new Constant.InvokeDynamic(reader.u2(), reader.u2());
            case 19 -> new Constant.ModuleRef(reader.u2());
            case 20 -> new Constant.PackageRef(reader.u2());
            default -> throw new ClassFormatException("unknown constant pool tag " + tag);
        };
    }

    private static String readModifiedUtf8(ByteReader reader) {
        int length = reader.u2();
        byte[] raw = reader.bytes(length);
        StringBuilder sb = new StringBuilder(length);
        int i = 0;
        while (i < raw.length) {
            int b1 = raw[i++] & 0xFF;
            if (b1 < 0x80) {
                if (b1 == 0) {
                    throw new ClassFormatException("raw zero byte in modified UTF-8 string");
                }
                sb.append((char) b1);
            } else if ((b1 & 0xE0) == 0xC0) {
                int b2 = nextContinuation(raw, i++);
                sb.append((char) (((b1 & 0x1F) << 6) | (b2 & 0x3F)));
            } else if ((b1 & 0xF0) == 0xE0) {
                int b2 = nextContinuation(raw, i++);
                int b3 = nextContinuation(raw, i++);
                sb.append((char) (((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F)));
            } else {
                throw new ClassFormatException(
                    String.format("illegal modified UTF-8 lead byte 0x%02X", b1));
            }
        }
        return sb.toString();
    }

    private static int nextContinuation(byte[] raw, int index) {
        if (index >= raw.length) {
            throw new ClassFormatException("truncated modified UTF-8 sequence");
        }
        int b = raw[index] & 0xFF;
        if ((b & 0xC0) != 0x80) {
            throw new ClassFormatException(
                String.format("illegal modified UTF-8 continuation byte 0x%02X", b));
        }
        return b;
    }

    private static List<MemberInfo> readMembers(ByteReader reader, ConstantPool pool) {
        int count = reader.u2();
        List<MemberInfo> members = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            int flags = reader.u2();
            String name = pool.utf8(reader.u2());
            String descriptor = pool.utf8(reader.u2());
            members.add(new MemberInfo(flags, name, descriptor, readAttributes(reader, pool)));
        }
        return List.copyOf(members);
    }

    private static List<AttributeInfo> readAttributes(ByteReader reader, ConstantPool pool) {
        int count = reader.u2();
        List<AttributeInfo> attributes = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            attributes.add(readAttribute(reader, pool));
        }
        return List.copyOf(attributes);
    }

    private static AttributeInfo readAttribute(ByteReader reader, ConstantPool pool) {
        String name = pool.utf8(reader.u2());
        long length = reader.u4();
        if (length > reader.remaining()) {
            throw new ClassFormatException("attribute " + name + " claims " + length
                + " byte(s) but only " + reader.remaining() + " remain");
        }
        int end = reader.position() + (int) length;
        AttributeInfo attribute = switch (name) {
            case "Code" -> readCode(reader, pool);
            case "LineNumberTable" -> readLineNumberTable(reader);
            case "LocalVariableTable" -> readLocalVariableTable(reader, pool);
            case "SourceFile" -> new AttributeInfo.SourceFile(pool.utf8(reader.u2()));
            case "ConstantValue" -> new AttributeInfo.ConstantValue(reader.u2());
            case "Exceptions" -> readExceptions(reader);
            case "Signature" -> new AttributeInfo.Signature(pool.utf8(reader.u2()));
            case "InnerClasses" -> readInnerClasses(reader);
            case "BootstrapMethods" -> readBootstrapMethods(reader);
            case "Deprecated" -> new AttributeInfo.Deprecated();
            case "Synthetic" -> new AttributeInfo.Synthetic();
            default -> new AttributeInfo.Raw(name, reader.bytes((int) length));
        };
        if (reader.position() != end) {
            reader.seek(end);
        }
        return attribute;
    }

    private static AttributeInfo readCode(ByteReader reader, ConstantPool pool) {
        int maxStack = reader.u2();
        int maxLocals = reader.u2();
        long codeLength = reader.u4();
        if (codeLength <= 0 || codeLength >= 65536) {
            throw new ClassFormatException("code_length out of range: " + codeLength);
        }
        byte[] code = reader.bytes((int) codeLength);
        int exceptionCount = reader.u2();
        List<AttributeInfo.ExceptionEntry> table = new ArrayList<>(exceptionCount);
        for (int i = 0; i < exceptionCount; i++) {
            table.add(new AttributeInfo.ExceptionEntry(reader.u2(), reader.u2(), reader.u2(), reader.u2()));
        }
        return new AttributeInfo.Code(maxStack, maxLocals, code, List.copyOf(table),
            readAttributes(reader, pool));
    }

    private static AttributeInfo readLineNumberTable(ByteReader reader) {
        int count = reader.u2();
        List<AttributeInfo.LineNumber> lines = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            lines.add(new AttributeInfo.LineNumber(reader.u2(), reader.u2()));
        }
        return new AttributeInfo.LineNumberTable(List.copyOf(lines));
    }

    private static AttributeInfo readLocalVariableTable(ByteReader reader, ConstantPool pool) {
        int count = reader.u2();
        List<AttributeInfo.LocalVariable> variables = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            int startPc = reader.u2();
            int length = reader.u2();
            String name = pool.utf8(reader.u2());
            String descriptor = pool.utf8(reader.u2());
            variables.add(new AttributeInfo.LocalVariable(startPc, length, name, descriptor, reader.u2()));
        }
        return new AttributeInfo.LocalVariableTable(List.copyOf(variables));
    }

    private static AttributeInfo readExceptions(ByteReader reader) {
        int count = reader.u2();
        List<Integer> indexes = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            indexes.add(reader.u2());
        }
        return new AttributeInfo.Exceptions(List.copyOf(indexes));
    }

    private static AttributeInfo readInnerClasses(ByteReader reader) {
        int count = reader.u2();
        List<AttributeInfo.InnerClassEntry> classes = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            classes.add(new AttributeInfo.InnerClassEntry(reader.u2(), reader.u2(), reader.u2(), reader.u2()));
        }
        return new AttributeInfo.InnerClasses(List.copyOf(classes));
    }

    private static AttributeInfo readBootstrapMethods(ByteReader reader) {
        int count = reader.u2();
        List<AttributeInfo.BootstrapMethod> methods = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            int handleIndex = reader.u2();
            int argumentCount = reader.u2();
            List<Integer> arguments = new ArrayList<>(argumentCount);
            for (int a = 0; a < argumentCount; a++) {
                arguments.add(reader.u2());
            }
            methods.add(new AttributeInfo.BootstrapMethod(handleIndex, List.copyOf(arguments)));
        }
        return new AttributeInfo.BootstrapMethods(List.copyOf(methods));
    }
}
