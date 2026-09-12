package classfile;

import java.util.ArrayList;
import java.util.List;

public final class AccessFlags {
    public static final int PUBLIC = 0x0001;
    public static final int PRIVATE = 0x0002;
    public static final int PROTECTED = 0x0004;
    public static final int STATIC = 0x0008;
    public static final int FINAL = 0x0010;
    public static final int SUPER = 0x0020;
    public static final int SYNCHRONIZED = 0x0020;
    public static final int VOLATILE = 0x0040;
    public static final int BRIDGE = 0x0040;
    public static final int TRANSIENT = 0x0080;
    public static final int VARARGS = 0x0080;
    public static final int NATIVE = 0x0100;
    public static final int INTERFACE = 0x0200;
    public static final int ABSTRACT = 0x0400;
    public static final int STRICT = 0x0800;
    public static final int SYNTHETIC = 0x1000;
    public static final int ANNOTATION = 0x2000;
    public static final int ENUM = 0x4000;
    public static final int MODULE = 0x8000;

    private AccessFlags() {
    }

    public static boolean has(int flags, int mask) {
        return (flags & mask) != 0;
    }

    public static List<String> forClass(int flags) {
        List<String> names = new ArrayList<>();
        add(names, flags, PUBLIC, "public");
        add(names, flags, FINAL, "final");
        add(names, flags, SUPER, "super");
        add(names, flags, INTERFACE, "interface");
        add(names, flags, ABSTRACT, "abstract");
        add(names, flags, SYNTHETIC, "synthetic");
        add(names, flags, ANNOTATION, "annotation");
        add(names, flags, ENUM, "enum");
        add(names, flags, MODULE, "module");
        return names;
    }

    public static List<String> forField(int flags) {
        List<String> names = new ArrayList<>();
        add(names, flags, PUBLIC, "public");
        add(names, flags, PRIVATE, "private");
        add(names, flags, PROTECTED, "protected");
        add(names, flags, STATIC, "static");
        add(names, flags, FINAL, "final");
        add(names, flags, VOLATILE, "volatile");
        add(names, flags, TRANSIENT, "transient");
        add(names, flags, SYNTHETIC, "synthetic");
        add(names, flags, ENUM, "enum");
        return names;
    }

    public static List<String> forMethod(int flags) {
        List<String> names = new ArrayList<>();
        add(names, flags, PUBLIC, "public");
        add(names, flags, PRIVATE, "private");
        add(names, flags, PROTECTED, "protected");
        add(names, flags, STATIC, "static");
        add(names, flags, FINAL, "final");
        add(names, flags, SYNCHRONIZED, "synchronized");
        add(names, flags, BRIDGE, "bridge");
        add(names, flags, VARARGS, "varargs");
        add(names, flags, NATIVE, "native");
        add(names, flags, ABSTRACT, "abstract");
        add(names, flags, STRICT, "strictfp");
        add(names, flags, SYNTHETIC, "synthetic");
        return names;
    }

    private static void add(List<String> names, int flags, int mask, String name) {
        if (has(flags, mask)) {
            names.add(name);
        }
    }
}
