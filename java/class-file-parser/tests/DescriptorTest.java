import classfile.ClassFormatException;
import classfile.Descriptor;
import java.util.List;

public final class DescriptorTest {

    private DescriptorTest() {
    }

    public static void run() {
        Assertions.suite("Descriptor");

        Assertions.test("decodes every primitive", () -> {
            Assertions.assertEquals("byte", "byte", Descriptor.prettyType("B"));
            Assertions.assertEquals("char", "char", Descriptor.prettyType("C"));
            Assertions.assertEquals("double", "double", Descriptor.prettyType("D"));
            Assertions.assertEquals("float", "float", Descriptor.prettyType("F"));
            Assertions.assertEquals("int", "int", Descriptor.prettyType("I"));
            Assertions.assertEquals("long", "long", Descriptor.prettyType("J"));
            Assertions.assertEquals("short", "short", Descriptor.prettyType("S"));
            Assertions.assertEquals("boolean", "boolean", Descriptor.prettyType("Z"));
            Assertions.assertEquals("void", "void", Descriptor.prettyType("V"));
        });

        Assertions.test("decodes object and array types", () -> {
            Assertions.assertEquals("object", "java.lang.String",
                Descriptor.prettyType("Ljava/lang/String;"));
            Assertions.assertEquals("array", "int[][]", Descriptor.prettyType("[[I"));
            Assertions.assertEquals("object array", "java.lang.Object[]",
                Descriptor.prettyType("[Ljava/lang/Object;"));
        });

        Assertions.test("parses a method descriptor", () -> {
            Descriptor.MethodSignature signature =
                Descriptor.parseMethod("(Ljava/lang/String;[IJ)Z");
            Assertions.assertEquals("parameters",
                List.of("java.lang.String", "int[]", "long"), signature.parameterTypes());
            Assertions.assertEquals("return type", "boolean", signature.returnType());
        });

        Assertions.test("counts long and double as two slots", () -> {
            Assertions.assertEquals("slots", 6,
                Descriptor.parseMethod("(IJDLjava/lang/String;)V").parameterSlots());
            Assertions.assertEquals("empty", 0, Descriptor.parseMethod("()V").parameterSlots());
        });

        Assertions.test("formats a readable method signature", () -> Assertions.assertEquals(
            "pretty", "void main(java.lang.String[])",
            Descriptor.prettyMethod("main", "([Ljava/lang/String;)V")));

        Assertions.test("rejects malformed descriptors", () -> {
            Assertions.assertThrows("missing paren", ClassFormatException.class,
                () -> Descriptor.parseMethod("IJ)V"));
            Assertions.assertThrows("unterminated object", ClassFormatException.class,
                () -> Descriptor.prettyType("Ljava/lang/String"));
            Assertions.assertThrows("unknown character", ClassFormatException.class,
                () -> Descriptor.prettyType("Q"));
            Assertions.assertThrows("trailing junk", ClassFormatException.class,
                () -> Descriptor.prettyType("IJ"));
            Assertions.assertThrows("empty object", ClassFormatException.class,
                () -> Descriptor.prettyType("L;"));
        });
    }
}
