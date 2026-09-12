import classfile.AccessFlags;
import classfile.ClassFile;
import classfile.ClassFileParser;
import classfile.ClassFormatException;
import classfile.ClassPrinter;
import classfile.Constant;
import classfile.MemberInfo;
import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;

public final class ParserTest {

    private ParserTest() {
    }

    public static ClassFile loadResource(String resource) {
        try (InputStream stream = ParserTest.class.getResourceAsStream(resource)) {
            if (stream == null) {
                throw new IllegalStateException("resource missing from the classpath: " + resource);
            }
            return ClassFileParser.parse(stream);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    public static void run() {
        Assertions.suite("ClassFileParser");

        Assertions.test("parses a synthetic minimal class", () -> {
            byte[] bytes = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "noop", "()V", 0, 1, new byte[]{(byte) 0xb1})
                .build();
            ClassFile parsed = ClassFileParser.parse(bytes);
            Assertions.assertEquals("name", "Demo", parsed.thisClassName());
            Assertions.assertEquals("super", "java.lang.Object", parsed.superClassName().orElseThrow());
            Assertions.assertEquals("methods", 1, parsed.methods().size());
            Assertions.assertEquals("major", 61, parsed.majorVersion());
            Assertions.assertEquals("java version", "17", parsed.javaVersion());
        });

        Assertions.test("rejects a bad magic number", () -> Assertions.assertThrows(
            "magic must be checked", ClassFormatException.class,
            () -> ClassFileParser.parse(new ClassFileBuilder().magic(0xDEADBEEFL).build())));

        Assertions.test("rejects a truncated file", () -> {
            byte[] full = new ClassFileBuilder().build();
            byte[] cut = new byte[full.length - 3];
            System.arraycopy(full, 0, cut, 0, cut.length);
            Assertions.assertThrows("truncation must be caught", ClassFormatException.class,
                () -> ClassFileParser.parse(cut));
        });

        Assertions.test("rejects trailing bytes", () -> {
            byte[] full = new ClassFileBuilder().build();
            byte[] padded = new byte[full.length + 2];
            System.arraycopy(full, 0, padded, 0, full.length);
            Assertions.assertThrows("trailing bytes must be caught", ClassFormatException.class,
                () -> ClassFileParser.parse(padded));
        });

        Assertions.test("gives long constants two pool slots", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int longIndex = builder.longEntry(42L);
            int afterIndex = builder.integerEntry(7);
            Assertions.assertEquals("slot skipped", longIndex + 2, afterIndex);
            ClassFile parsed = ClassFileParser.parse(builder.build());
            Assertions.assertEquals("long value", 42L,
                parsed.constantPool().at(longIndex, Constant.LongValue.class).value());
            Assertions.assertFalse("second slot is dead",
                parsed.constantPool().validIndex(longIndex + 1));
            Assertions.assertThrows("dead slot must throw", ClassFormatException.class,
                () -> parsed.constantPool().at(longIndex + 1));
        });

        Assertions.test("parses fields with their flags", () -> {
            byte[] bytes = new ClassFileBuilder()
                .field(AccessFlags.PRIVATE | AccessFlags.STATIC | AccessFlags.FINAL, "count", "J")
                .build();
            MemberInfo field = ClassFileParser.parse(bytes).fields().get(0);
            Assertions.assertEquals("pretty", "private static final long count", field.prettyField());
        });

        Assertions.test("parses its own compiled class file", () -> {
            ClassFile parsed = loadResource("/classfile/Disassembler.class");
            Assertions.assertEquals("name", "classfile.Disassembler", parsed.thisClassName());
            Assertions.assertTrue("has a decode method", parsed.findMethod("decode").isPresent());
            Assertions.assertEquals("source", "Disassembler.java", parsed.sourceFile().orElseThrow());
            Assertions.assertTrue("pool is populated", parsed.constantPool().size() > 20);
        });

        Assertions.test("reads a nested record class", () -> {
            ClassFile parsed = loadResource("/classfile/Constant$Utf8.class");
            Assertions.assertEquals("name", "classfile.Constant$Utf8", parsed.thisClassName());
            Assertions.assertEquals("super", "java.lang.Record", parsed.superClassName().orElseThrow());
            Assertions.assertTrue("implements Constant",
                parsed.interfaceNames().contains("classfile.Constant"));
        });

        Assertions.test("reads a class out of the JDK runtime image", () -> {
            ClassFile parsed = loadResource("/java/lang/Runnable.class");
            Assertions.assertEquals("name", "java.lang.Runnable", parsed.thisClassName());
            Assertions.assertTrue("is an interface", parsed.isInterface());
            Assertions.assertTrue("declares run", parsed.findMethod("run").isPresent());
        });

        Assertions.test("prints a header without duplicating the interface keyword", () -> {
            String header = new ClassPrinter(loadResource("/java/lang/Runnable.class")).header().get(0);
            Assertions.assertEquals("header", "public abstract interface java.lang.Runnable", header);
            String plain = new ClassPrinter(loadResource("/classfile/Opcode.class")).header().get(0);
            Assertions.assertEquals("class header", "public final class classfile.Opcode", plain);
        });

        Assertions.test("keeps the constant pool listing aligned with its entries", () -> {
            ClassFile parsed = loadResource("/classfile/Descriptor.class");
            Assertions.assertFalse("listing is not empty", parsed.constantPool().listing().isEmpty());
            Assertions.assertTrue("listing fits the pool",
                parsed.constantPool().listing().size() < parsed.constantPool().size());
        });
    }
}
