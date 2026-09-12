import classfile.AccessFlags;
import classfile.ClassFile;
import classfile.ClassFileParser;
import classfile.Verifier;
import java.util.List;

public final class VerifierTest {

    private VerifierTest() {
    }

    private static List<String> verify(ClassFileBuilder builder) {
        return Verifier.verify(ClassFileParser.parse(builder.build()));
    }

    private static boolean mentions(List<String> problems, String fragment) {
        return problems.stream().anyMatch(problem -> problem.contains(fragment));
    }

    public static void run() {
        Assertions.suite("Verifier");

        Assertions.test("accepts a well formed class", () -> {
            List<String> problems = verify(new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "noop", "()V", 1, 1, new byte[]{(byte) 0xb1}));
            Assertions.assertEquals("no problems", List.of(), problems);
        });

        Assertions.test("flags an unsupported class file version", () -> Assertions.assertTrue(
            "version reported",
            mentions(verify(new ClassFileBuilder().majorVersion(99)), "unsupported major version")));

        Assertions.test("flags a class that is both final and abstract", () -> Assertions.assertTrue(
            "conflict reported",
            mentions(verify(new ClassFileBuilder().accessFlags(AccessFlags.FINAL | AccessFlags.ABSTRACT)),
                "final and abstract")));

        Assertions.test("flags a non abstract interface", () -> Assertions.assertTrue(
            "interface must be abstract",
            mentions(verify(new ClassFileBuilder().accessFlags(AccessFlags.INTERFACE)),
                "interface must be abstract")));

        Assertions.test("flags this_class pointing at a non class entry", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int utf8Index = builder.utf8("not a class");
            Assertions.assertTrue("bad this_class reported",
                mentions(verify(builder.thisClassIndex(utf8Index)), "this_class must point to a Class"));
        });

        Assertions.test("flags a missing super class on an ordinary class", () -> Assertions.assertTrue(
            "super 0 reported",
            mentions(verify(new ClassFileBuilder().superClassIndex(0)),
                "only java.lang.Object may have super_class 0")));

        Assertions.test("flags duplicate members", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .field(AccessFlags.PRIVATE, "value", "I")
                .field(AccessFlags.PUBLIC, "value", "I");
            Assertions.assertTrue("duplicate reported", mentions(verify(builder), "duplicate field"));
        });

        Assertions.test("flags an invalid method descriptor", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .abstractMethod(AccessFlags.PUBLIC | AccessFlags.ABSTRACT, "broken", "(Q)V");
            Assertions.assertTrue("descriptor reported",
                mentions(verify(builder), "invalid descriptor"));
        });

        Assertions.test("flags an abstract method that carries code", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC | AccessFlags.ABSTRACT, "ghost", "()V", 1, 1,
                    new byte[]{(byte) 0xb1});
            Assertions.assertTrue("code on abstract reported",
                mentions(verify(builder), "must not have a Code attribute"));
        });

        Assertions.test("flags a concrete method with no code", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .abstractMethod(AccessFlags.PUBLIC, "empty", "()V");
            Assertions.assertTrue("missing code reported",
                mentions(verify(builder), "neither Code nor abstract/native"));
        });

        Assertions.test("flags max_locals smaller than the argument slots", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "takes", "(JJ)V", 1, 2, new byte[]{(byte) 0xb1});
            Assertions.assertTrue("max_locals reported",
                mentions(verify(builder), "declares max_locals 2 but its arguments need 5"));
        });

        Assertions.test("flags code that falls off the end", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "runs", "()V", 1, 1, new byte[]{0x00, 0x00});
            Assertions.assertTrue("fall off reported",
                mentions(verify(builder), "falls off the end"));
        });

        Assertions.test("flags a branch into the middle of an instruction", () -> {
            ClassFileBuilder builder = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "jumps", "()V", 1, 1,
                    new byte[]{(byte) 0xa7, 0x00, 0x02, (byte) 0xb1});
            Assertions.assertTrue("misaligned branch reported",
                mentions(verify(builder), "not an instruction boundary"));
        });

        Assertions.test("flags an inverted exception range", () -> {
            int[][] handlers = {{3, 1, 3, 0}};
            ClassFileBuilder builder = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "guarded", "()V", 1, 1,
                    new byte[]{0x00, 0x00, 0x00, (byte) 0xb1}, handlers);
            Assertions.assertTrue("range reported",
                mentions(verify(builder), "is empty or inverted"));
        });

        Assertions.test("accepts a valid exception handler", () -> {
            int[][] handlers = {{0, 3, 3, 0}};
            ClassFileBuilder builder = new ClassFileBuilder()
                .method(AccessFlags.PUBLIC, "guarded", "()V", 1, 1,
                    new byte[]{0x00, 0x00, 0x00, (byte) 0xb1}, handlers);
            Assertions.assertEquals("clean", List.of(), verify(builder));
        });

        Assertions.test("reports nothing for real javac output", () -> {
            for (String resource : List.of("/classfile/ByteReader.class", "/classfile/Opcode.class",
                "/classfile/ClassPrinter.class", "/classfile/AccessFlags.class", "/Main.class")) {
                ClassFile parsed = ParserTest.loadResource(resource);
                Assertions.assertEquals("clean for " + resource, List.of(), Verifier.verify(parsed));
            }
        });

        Assertions.test("reports nothing for a JDK interface", () -> {
            ClassFile parsed = ParserTest.loadResource("/java/lang/Comparable.class");
            Assertions.assertEquals("clean", List.of(), Verifier.verify(parsed));
        });
    }
}
