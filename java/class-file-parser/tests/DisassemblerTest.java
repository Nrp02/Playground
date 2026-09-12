import classfile.AccessFlags;
import classfile.AttributeInfo;
import classfile.ClassFile;
import classfile.ClassFileParser;
import classfile.ClassFormatException;
import classfile.Disassembler;
import classfile.Instruction;
import classfile.MemberInfo;
import classfile.Opcode;
import java.util.List;

public final class DisassemblerTest {

    private DisassemblerTest() {
    }

    private static List<Instruction> decode(byte[] code) {
        ClassFile parsed = ClassFileParser.parse(new ClassFileBuilder()
            .method(AccessFlags.PUBLIC, "body", "()V", 4, 4, code)
            .build());
        AttributeInfo.Code attribute = parsed.findMethod("body").orElseThrow().code().orElseThrow();
        return new Disassembler(parsed.constantPool()).decode(attribute.code());
    }

    public static void run() {
        Assertions.suite("Disassembler");

        Assertions.test("the opcode table covers the defined range", () -> {
            Assertions.assertEquals("count", 202, Opcode.definedCount());
            Assertions.assertEquals("invokevirtual", "invokevirtual", Opcode.mnemonic(0xb6));
            Assertions.assertEquals("aload_0", "aload_0", Opcode.mnemonic(0x2a));
            Assertions.assertEquals("return", "return", Opcode.mnemonic(0xb1));
            Assertions.assertFalse("0xca is undefined", Opcode.isDefined(0xca));
        });

        Assertions.test("decodes single byte instructions", () -> {
            List<Instruction> decoded = decode(new byte[]{0x00, 0x01, 0x03, (byte) 0xb1});
            Assertions.assertEquals("count", 4, decoded.size());
            Assertions.assertEquals("first", "nop", decoded.get(0).mnemonic());
            Assertions.assertEquals("last pc", 3, decoded.get(3).pc());
            Assertions.assertTrue("last returns", decoded.get(3).isReturn());
        });

        Assertions.test("decodes immediate operands", () -> {
            List<Instruction> decoded = decode(new byte[]{
                0x10, (byte) 0xF6, 0x11, 0x01, 0x00, (byte) 0xb1});
            Assertions.assertEquals("bipush", "bipush -10", decoded.get(0).text());
            Assertions.assertEquals("sipush", "sipush 256", decoded.get(1).text());
            Assertions.assertEquals("bipush length", 2, decoded.get(0).length());
        });

        Assertions.test("resolves constant pool references", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int methodIndex = builder.methodRef("java/lang/Object", "hashCode", "()I");
            byte[] code = {0x2a, (byte) 0xb6, (byte) (methodIndex >> 8), (byte) methodIndex,
                0x57, (byte) 0xb1};
            ClassFile parsed = ClassFileParser.parse(builder
                .method(AccessFlags.PUBLIC, "call", "()V", 2, 1, code).build());
            List<Instruction> decoded = new Disassembler(parsed.constantPool())
                .decode(parsed.findMethod("call").orElseThrow().code().orElseThrow().code());
            Assertions.assertTrue("mentions the target",
                decoded.get(1).text().contains("java.lang.Object.int hashCode()"));
        });

        Assertions.test("computes branch targets from relative offsets", () -> {
            List<Instruction> decoded = decode(new byte[]{
                0x03, (byte) 0x99, 0x00, 0x04, (byte) 0xb1, (byte) 0xb1});
            Instruction branch = decoded.get(1);
            Assertions.assertTrue("is a branch", branch.isBranch());
            Assertions.assertEquals("target", List.of(5), branch.branchTargets());
            Assertions.assertEquals("rendered", "ifeq -> 5", branch.text());
        });

        Assertions.test("decodes iinc and wide iinc", () -> {
            List<Instruction> narrow = decode(new byte[]{(byte) 0x84, 0x02, (byte) 0xFF, (byte) 0xb1});
            Assertions.assertEquals("iinc", "iinc slot 2 by -1", narrow.get(0).text());
            List<Instruction> wide = decode(new byte[]{
                (byte) 0xc4, (byte) 0x84, 0x01, 0x00, 0x00, 0x05, (byte) 0xb1});
            Assertions.assertEquals("wide iinc", "wide iinc slot 256 by 5", wide.get(0).text());
            Assertions.assertTrue("flagged wide", wide.get(0).wide());
            Assertions.assertEquals("wide length", 6, wide.get(0).length());
        });

        Assertions.test("decodes a padded tableswitch", () -> {
            byte[] code = {
                0x03, (byte) 0xaa, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x17,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x17,
                0x00, 0x00, 0x00, 0x17,
                (byte) 0xb1};
            List<Instruction> decoded = decode(code);
            Instruction table = decoded.get(1);
            Assertions.assertEquals("mnemonic", "tableswitch", table.mnemonic());
            Assertions.assertTrue("describes the range", table.text().contains("range 0..1"));
            Assertions.assertEquals("default and two cases", List.of(24, 24, 24), table.branchTargets());
            Assertions.assertEquals("consumed the padding and the jump table", 23, table.length());
            Assertions.assertEquals("return follows", 24, decoded.get(2).pc());
        });

        Assertions.test("decodes a lookupswitch", () -> {
            byte[] code = {
                0x03, (byte) 0xab, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x13,
                0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x07,
                0x00, 0x00, 0x00, 0x13,
                (byte) 0xb1};
            List<Instruction> decoded = decode(code);
            Instruction lookup = decoded.get(1);
            Assertions.assertTrue("describes the pairs", lookup.text().contains("1 pairs"));
            Assertions.assertEquals("default and one match", List.of(20, 20), lookup.branchTargets());
            Assertions.assertEquals("return follows", 20, decoded.get(2).pc());
        });

        Assertions.test("rejects an undefined opcode", () -> Assertions.assertThrows(
            "0xca must be refused", ClassFormatException.class,
            () -> decode(new byte[]{(byte) 0xca, (byte) 0xb1})));

        Assertions.test("rejects a branch that leaves the code array", () -> Assertions.assertThrows(
            "out of range branch", ClassFormatException.class,
            () -> decode(new byte[]{(byte) 0xa7, 0x7F, (byte) 0xFF, (byte) 0xb1})));

        Assertions.test("rejects invokeinterface without its zero byte", () -> {
            ClassFileBuilder builder = new ClassFileBuilder();
            int ref = builder.methodRef("java/util/List", "size", "()I");
            byte[] code = {(byte) 0xb9, (byte) (ref >> 8), (byte) ref, 0x01, 0x09, (byte) 0xb1};
            ClassFile parsed = ClassFileParser.parse(builder
                .method(AccessFlags.PUBLIC, "bad", "()V", 2, 1, code).build());
            Assertions.assertThrows("trailing byte must be zero", ClassFormatException.class,
                () -> new Disassembler(parsed.constantPool())
                    .decode(parsed.findMethod("bad").orElseThrow().code().orElseThrow().code()));
        });

        Assertions.test("round trips every method of a real class", () -> {
            ClassFile parsed = ParserTest.loadResource("/classfile/ClassFileParser.class");
            Disassembler disassembler = new Disassembler(parsed.constantPool());
            int instructionCount = 0;
            for (MemberInfo method : parsed.methods()) {
                if (method.code().isEmpty()) {
                    continue;
                }
                AttributeInfo.Code code = method.code().orElseThrow();
                List<Instruction> decoded = disassembler.decode(code.code());
                instructionCount += decoded.size();
                int consumed = decoded.stream().mapToInt(Instruction::length).sum();
                Assertions.assertEquals("decoded length matches the code array for " + method.name(),
                    code.code().length, consumed);
            }
            Assertions.assertTrue("decoded a real amount of bytecode", instructionCount > 200);
        });

        Assertions.test("marks branch targets in the listing", () -> {
            ClassFile parsed = ParserTest.loadResource("/classfile/Verifier.class");
            MemberInfo method = parsed.findMethod("checkCode").orElseThrow();
            List<String> listing = new Disassembler(parsed.constantPool())
                .listing(method.code().orElseThrow());
            Assertions.assertTrue("listing has lines", listing.size() > 10);
            Assertions.assertTrue("some line is marked as a target",
                listing.stream().anyMatch(line -> line.startsWith(">")));
        });
    }
}
