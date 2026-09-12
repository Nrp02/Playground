package classfile;

import java.util.List;

public final class ConstantPool {
    private final Constant[] entries;

    public ConstantPool(Constant[] entries) {
        this.entries = entries.clone();
    }

    public int size() {
        return entries.length;
    }

    public boolean validIndex(int index) {
        return index > 0 && index < entries.length && !(entries[index] instanceof Constant.Unusable);
    }

    public Constant at(int index) {
        if (index <= 0 || index >= entries.length) {
            throw new ClassFormatException("constant pool index out of range: " + index);
        }
        Constant entry = entries[index];
        if (entry instanceof Constant.Unusable) {
            throw new ClassFormatException("constant pool index " + index + " is an unusable slot");
        }
        return entry;
    }

    public <T extends Constant> T at(int index, Class<T> expected) {
        Constant entry = at(index);
        if (!expected.isInstance(entry)) {
            throw new ClassFormatException("constant pool index " + index + " is "
                + entry.getClass().getSimpleName() + ", expected " + expected.getSimpleName());
        }
        return expected.cast(entry);
    }

    public String utf8(int index) {
        return at(index, Constant.Utf8.class).value();
    }

    public String className(int index) {
        return utf8(at(index, Constant.ClassRef.class).nameIndex()).replace('/', '.');
    }

    public String memberName(int classIndex, int nameAndTypeIndex) {
        Constant.NameAndType nat = at(nameAndTypeIndex, Constant.NameAndType.class);
        return className(classIndex) + "." + utf8(nat.nameIndex());
    }

    public String describe(int index) {
        Constant entry = at(index);
        return switch (entry) {
            case Constant.Utf8 c -> quote(c.value());
            case Constant.IntegerValue c -> Integer.toString(c.value());
            case Constant.FloatValue c -> c.value() + "f";
            case Constant.LongValue c -> c.value() + "L";
            case Constant.DoubleValue c -> Double.toString(c.value());
            case Constant.ClassRef c -> utf8(c.nameIndex()).replace('/', '.');
            case Constant.StringRef c -> quote(utf8(c.utf8Index()));
            case Constant.FieldRef c -> memberDescription(c.classIndex(), c.nameAndTypeIndex());
            case Constant.MethodRef c -> memberDescription(c.classIndex(), c.nameAndTypeIndex());
            case Constant.InterfaceMethodRef c -> memberDescription(c.classIndex(), c.nameAndTypeIndex());
            case Constant.NameAndType c -> utf8(c.nameIndex()) + ":" + utf8(c.descriptorIndex());
            case Constant.MethodHandle c -> handleKind(c.referenceKind()) + " " + describe(c.referenceIndex());
            case Constant.MethodType c -> utf8(c.descriptorIndex());
            case Constant.DynamicValue c -> "bsm#" + c.bootstrapMethodAttrIndex() + " "
                + describe(c.nameAndTypeIndex());
            case Constant.InvokeDynamic c -> "bsm#" + c.bootstrapMethodAttrIndex() + " "
                + describe(c.nameAndTypeIndex());
            case Constant.ModuleRef c -> utf8(c.nameIndex());
            case Constant.PackageRef c -> utf8(c.nameIndex()).replace('/', '.');
            case Constant.Unusable ignored -> "<unusable>";
        };
    }

    private String memberDescription(int classIndex, int nameAndTypeIndex) {
        Constant.NameAndType nat = at(nameAndTypeIndex, Constant.NameAndType.class);
        String descriptor = utf8(nat.descriptorIndex());
        String name = utf8(nat.nameIndex());
        String owner = className(classIndex);
        if (descriptor.startsWith("(")) {
            return owner + "." + Descriptor.prettyMethod(name, descriptor);
        }
        return Descriptor.prettyType(descriptor) + " " + owner + "." + name;
    }

    public static String handleKind(int kind) {
        return switch (kind) {
            case 1 -> "getField";
            case 2 -> "getStatic";
            case 3 -> "putField";
            case 4 -> "putStatic";
            case 5 -> "invokeVirtual";
            case 6 -> "invokeStatic";
            case 7 -> "invokeSpecial";
            case 8 -> "newInvokeSpecial";
            case 9 -> "invokeInterface";
            default -> "kind" + kind;
        };
    }

    public static String tagName(int tag) {
        return switch (tag) {
            case 1 -> "Utf8";
            case 3 -> "Integer";
            case 4 -> "Float";
            case 5 -> "Long";
            case 6 -> "Double";
            case 7 -> "Class";
            case 8 -> "String";
            case 9 -> "Fieldref";
            case 10 -> "Methodref";
            case 11 -> "InterfaceMethodref";
            case 12 -> "NameAndType";
            case 15 -> "MethodHandle";
            case 16 -> "MethodType";
            case 17 -> "Dynamic";
            case 18 -> "InvokeDynamic";
            case 19 -> "Module";
            case 20 -> "Package";
            default -> "Unknown";
        };
    }

    public List<String> listing() {
        return java.util.stream.IntStream.range(1, entries.length)
            .filter(i -> !(entries[i] instanceof Constant.Unusable))
            .mapToObj(i -> String.format("#%-4d %-18s %s", i, tagName(entries[i].tag()), describe(i)))
            .toList();
    }

    private static String quote(String value) {
        StringBuilder sb = new StringBuilder("\"");
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '\n' -> sb.append("\\n");
                case '\r' -> sb.append("\\r");
                case '\t' -> sb.append("\\t");
                case '"' -> sb.append("\\\"");
                case '\\' -> sb.append("\\\\");
                default -> sb.append(c);
            }
        }
        return sb.append('"').toString();
    }
}
