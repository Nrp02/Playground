import classfile.ClassFile;
import classfile.ClassFileParser;
import classfile.ClassFormatException;
import classfile.Constant;
import classfile.ConstantPool;

public final class ConstantPoolTest {

    private ConstantPoolTest() {
    }

    public static void run() {
        Assertions.suite("ConstantPool");

        Assertions.test("names every tag it can parse", () -> {
            Assertions.assertEquals("utf8", "Utf8", ConstantPool.tagName(1));
            Assertions.assertEquals("methodref", "Methodref", ConstantPool.tagName(10));
            Assertions.assertEquals("invokedynamic", "InvokeDynamic", ConstantPool.tagName(18));
            Assertions.assertEquals("unknown", "Unknown", ConstantPool.tagName(99));
        });

        Assertions.test("names method handle kinds", () -> {
            Assertions.assertEquals("kind 6", "invokeStatic", ConstantPool.handleKind(6));
            Assertions.assertEquals("kind 8", "newInvokeSpecial", ConstantPool.handleKind(8));
        });

        Assertions.test("resolves class names to dotted form", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int index = builder.classEntry("java/util/concurrent/ConcurrentHashMap");
            ClassFile parsed = ClassFileParser.parse(builder.build());
            Assertions.assertEquals("dotted", "java.util.concurrent.ConcurrentHashMap",
                parsed.constantPool().className(index));
        });

        Assertions.test("describes a field reference with its type", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int index = builder.fieldRef("java/lang/System", "out", "Ljava/io/PrintStream;");
            ClassFile parsed = ClassFileParser.parse(builder.build());
            Assertions.assertEquals("described", "java.io.PrintStream java.lang.System.out",
                parsed.constantPool().describe(index));
        });

        Assertions.test("escapes control characters in string constants", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int index = builder.stringEntry("a\nb\tc\"d");
            ClassFile parsed = ClassFileParser.parse(builder.build());
            Assertions.assertEquals("escaped", "\"a\\nb\\tc\\\"d\"",
                parsed.constantPool().describe(index));
        });

        Assertions.test("type checks a requested entry", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int index = builder.integerEntry(5);
            ClassFile parsed = ClassFileParser.parse(builder.build());
            Assertions.assertEquals("as integer", 5,
                parsed.constantPool().at(index, Constant.IntegerValue.class).value());
            Assertions.assertThrows("wrong type must throw", ClassFormatException.class,
                () -> parsed.constantPool().at(index, Constant.Utf8.class));
        });

        Assertions.test("rejects out of range indexes", () -> {
            ClassFile parsed = ClassFileParser.parse(new ClassFileBuilder().build());
            Assertions.assertThrows("zero", ClassFormatException.class,
                () -> parsed.constantPool().at(0));
            Assertions.assertThrows("past the end", ClassFormatException.class,
                () -> parsed.constantPool().at(parsed.constantPool().size() + 5));
            Assertions.assertFalse("validIndex agrees", parsed.constantPool().validIndex(0));
        });

        Assertions.test("round trips a modified UTF-8 string with non ascii text", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int index = builder.utf8("café ไทย");
            ClassFile parsed = ClassFileParser.parse(builder.build());
            Assertions.assertEquals("decoded", "café ไทย",
                parsed.constantPool().utf8(index));
        });
    }
}
