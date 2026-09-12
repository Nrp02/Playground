import classfile.AttributeInfo;
import classfile.ClassFile;
import classfile.ClassFileParser;
import classfile.ClassPrinter;
import classfile.MemberInfo;
import classfile.Opcode;
import classfile.Verifier;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.Optional;

public final class Main {

    public static void main(String[] args) throws IOException {
        section("opcode table");
        System.out.println("  " + Opcode.definedCount() + " opcodes defined, "
            + "0xb6 is " + Opcode.mnemonic(0xb6) + ", 0xaa is " + Opcode.mnemonic(0xaa));

        ClassFile self = load("/Main.class");
        report(self, "main");

        ClassFile disassembler = load("/classfile/Disassembler.class");
        report(disassembler, "constantText");

        ClassFile constant = load("/classfile/Constant$Utf8.class");
        section("nested record " + constant.thisClassName());
        new ClassPrinter(constant).header().forEach(System.out::println);
        new ClassPrinter(constant).fields().forEach(System.out::println);

        loadFromRuntime().ifPresent(runtime -> {
            section("a class straight out of the JDK image");
            new ClassPrinter(runtime).header().forEach(System.out::println);
            System.out.println("  first five methods:");
            runtime.methods().stream().limit(5)
                .forEach(m -> System.out.println("    " + m.prettyMethod()));
        });

        section("rejecting a broken class file");
        byte[] broken = {0x00, 0x01, 0x02, 0x03};
        try {
            ClassFileParser.parse(broken);
            System.out.println("  no error raised, which would be a bug");
        } catch (RuntimeException e) {
            System.out.println("  " + e.getMessage());
        }
    }

    private static void report(ClassFile classFile, String methodName) {
        section(classFile.thisClassName());
        ClassPrinter printer = new ClassPrinter(classFile);
        printer.header().forEach(System.out::println);
        if (!printer.fields().isEmpty()) {
            System.out.println("  fields:");
            printer.fields().forEach(line -> System.out.println("  " + line));
        }
        System.out.println("  attributes: " + printer.attributeNames());

        Optional<MemberInfo> method = classFile.findMethod(methodName);
        if (method.isPresent()) {
            System.out.println();
            printer.disassemble(method.get()).stream().limit(24).forEach(System.out::println);
            int total = method.get().code().map(AttributeInfo.Code::code).map(c -> c.length).orElse(0);
            System.out.println("  (code array is " + total + " bytes)");
        }

        List<String> problems = Verifier.verify(classFile);
        System.out.println();
        System.out.println("  verifier: " + (problems.isEmpty() ? "clean" : problems.toString()));
    }

    private static ClassFile load(String resource) throws IOException {
        try (InputStream stream = Main.class.getResourceAsStream(resource)) {
            if (stream == null) {
                throw new IOException("resource not on the classpath: " + resource);
            }
            return ClassFileParser.parse(stream);
        }
    }

    private static Optional<ClassFile> loadFromRuntime() {
        try (InputStream stream = Object.class.getResourceAsStream("/java/lang/Runnable.class")) {
            return stream == null ? Optional.empty() : Optional.of(ClassFileParser.parse(stream));
        } catch (IOException | RuntimeException e) {
            return Optional.empty();
        }
    }

    private static void section(String title) {
        System.out.println();
        System.out.println("=== " + title + " ===");
    }
}
