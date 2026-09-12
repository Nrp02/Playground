package classfile;

public final class Opcode {

    public enum Operands {
        NONE, BYTE, SHORT, LOCAL, CONSTANT_1, CONSTANT_2, BRANCH_2, BRANCH_4, IINC,
        NEWARRAY, MULTIANEWARRAY, INVOKE_INTERFACE, INVOKE_DYNAMIC, TABLESWITCH, LOOKUPSWITCH, WIDE
    }

    public record Entry(int code, String mnemonic, Operands operands) {
    }

    private static final Entry[] TABLE = new Entry[256];

    private Opcode() {
    }

    private static void define(int code, String mnemonic, Operands operands) {
        TABLE[code] = new Entry(code, mnemonic, operands);
    }

    private static void defineSequence(int start, Operands operands, String... mnemonics) {
        for (int i = 0; i < mnemonics.length; i++) {
            define(start + i, mnemonics[i], operands);
        }
    }

    static {
        defineSequence(0x00, Operands.NONE, "nop", "aconst_null", "iconst_m1", "iconst_0", "iconst_1",
            "iconst_2", "iconst_3", "iconst_4", "iconst_5", "lconst_0", "lconst_1", "fconst_0",
            "fconst_1", "fconst_2", "dconst_0", "dconst_1");
        define(0x10, "bipush", Operands.BYTE);
        define(0x11, "sipush", Operands.SHORT);
        define(0x12, "ldc", Operands.CONSTANT_1);
        define(0x13, "ldc_w", Operands.CONSTANT_2);
        define(0x14, "ldc2_w", Operands.CONSTANT_2);
        defineSequence(0x15, Operands.LOCAL, "iload", "lload", "fload", "dload", "aload");
        defineSequence(0x1a, Operands.NONE, "iload_0", "iload_1", "iload_2", "iload_3",
            "lload_0", "lload_1", "lload_2", "lload_3", "fload_0", "fload_1", "fload_2", "fload_3",
            "dload_0", "dload_1", "dload_2", "dload_3", "aload_0", "aload_1", "aload_2", "aload_3",
            "iaload", "laload", "faload", "daload", "aaload", "baload", "caload", "saload");
        defineSequence(0x36, Operands.LOCAL, "istore", "lstore", "fstore", "dstore", "astore");
        defineSequence(0x3b, Operands.NONE, "istore_0", "istore_1", "istore_2", "istore_3",
            "lstore_0", "lstore_1", "lstore_2", "lstore_3", "fstore_0", "fstore_1", "fstore_2",
            "fstore_3", "dstore_0", "dstore_1", "dstore_2", "dstore_3", "astore_0", "astore_1",
            "astore_2", "astore_3", "iastore", "lastore", "fastore", "dastore", "aastore",
            "bastore", "castore", "sastore", "pop", "pop2", "dup", "dup_x1", "dup_x2", "dup2",
            "dup2_x1", "dup2_x2", "swap", "iadd", "ladd", "fadd", "dadd", "isub", "lsub", "fsub",
            "dsub", "imul", "lmul", "fmul", "dmul", "idiv", "ldiv", "fdiv", "ddiv", "irem", "lrem",
            "frem", "drem", "ineg", "lneg", "fneg", "dneg", "ishl", "lshl", "ishr", "lshr", "iushr",
            "lushr", "iand", "land", "ior", "lor", "ixor", "lxor");
        define(0x84, "iinc", Operands.IINC);
        defineSequence(0x85, Operands.NONE, "i2l", "i2f", "i2d", "l2i", "l2f", "l2d", "f2i", "f2l",
            "f2d", "d2i", "d2l", "d2f", "i2b", "i2c", "i2s", "lcmp", "fcmpl", "fcmpg", "dcmpl", "dcmpg");
        defineSequence(0x99, Operands.BRANCH_2, "ifeq", "ifne", "iflt", "ifge", "ifgt", "ifle",
            "if_icmpeq", "if_icmpne", "if_icmplt", "if_icmpge", "if_icmpgt", "if_icmple",
            "if_acmpeq", "if_acmpne", "goto", "jsr");
        define(0xa9, "ret", Operands.LOCAL);
        define(0xaa, "tableswitch", Operands.TABLESWITCH);
        define(0xab, "lookupswitch", Operands.LOOKUPSWITCH);
        defineSequence(0xac, Operands.NONE, "ireturn", "lreturn", "freturn", "dreturn", "areturn", "return");
        defineSequence(0xb2, Operands.CONSTANT_2, "getstatic", "putstatic", "getfield", "putfield",
            "invokevirtual", "invokespecial", "invokestatic");
        define(0xb9, "invokeinterface", Operands.INVOKE_INTERFACE);
        define(0xba, "invokedynamic", Operands.INVOKE_DYNAMIC);
        define(0xbb, "new", Operands.CONSTANT_2);
        define(0xbc, "newarray", Operands.NEWARRAY);
        define(0xbd, "anewarray", Operands.CONSTANT_2);
        define(0xbe, "arraylength", Operands.NONE);
        define(0xbf, "athrow", Operands.NONE);
        define(0xc0, "checkcast", Operands.CONSTANT_2);
        define(0xc1, "instanceof", Operands.CONSTANT_2);
        define(0xc2, "monitorenter", Operands.NONE);
        define(0xc3, "monitorexit", Operands.NONE);
        define(0xc4, "wide", Operands.WIDE);
        define(0xc5, "multianewarray", Operands.MULTIANEWARRAY);
        define(0xc6, "ifnull", Operands.BRANCH_2);
        define(0xc7, "ifnonnull", Operands.BRANCH_2);
        define(0xc8, "goto_w", Operands.BRANCH_4);
        define(0xc9, "jsr_w", Operands.BRANCH_4);
    }

    public static boolean isDefined(int code) {
        return code >= 0 && code < TABLE.length && TABLE[code] != null;
    }

    public static Entry of(int code) {
        if (!isDefined(code)) {
            throw new ClassFormatException(String.format("undefined opcode 0x%02X", code));
        }
        return TABLE[code];
    }

    public static String mnemonic(int code) {
        return isDefined(code) ? TABLE[code].mnemonic() : String.format("<0x%02X>", code);
    }

    public static int definedCount() {
        int count = 0;
        for (Entry entry : TABLE) {
            if (entry != null) {
                count++;
            }
        }
        return count;
    }

    public static String arrayTypeName(int atype) {
        return switch (atype) {
            case 4 -> "boolean";
            case 5 -> "char";
            case 6 -> "float";
            case 7 -> "double";
            case 8 -> "byte";
            case 9 -> "short";
            case 10 -> "int";
            case 11 -> "long";
            default -> throw new ClassFormatException("unknown newarray type " + atype);
        };
    }
}
