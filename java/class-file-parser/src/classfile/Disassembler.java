package classfile;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public final class Disassembler {
    private final ConstantPool pool;

    public Disassembler(ConstantPool pool) {
        this.pool = pool;
    }

    public List<Instruction> decode(byte[] code) {
        ByteReader reader = new ByteReader(code);
        List<Instruction> instructions = new ArrayList<>();
        while (reader.hasRemaining()) {
            instructions.add(decodeOne(reader, code.length));
        }
        return List.copyOf(instructions);
    }

    private Instruction decodeOne(ByteReader reader, int codeLength) {
        int pc = reader.position();
        int opcode = reader.u1();
        if (!Opcode.isDefined(opcode)) {
            throw new ClassFormatException(
                String.format("undefined opcode 0x%02X at pc %d", opcode, pc));
        }
        Opcode.Entry entry = Opcode.of(opcode);
        List<Integer> targets = new ArrayList<>();
        boolean wide = false;
        String operandText = switch (entry.operands()) {
            case NONE -> "";
            case BYTE -> Integer.toString(reader.s1());
            case SHORT -> Integer.toString(reader.s2());
            case LOCAL -> "slot " + reader.u1();
            case CONSTANT_1 -> constantText(reader.u1());
            case CONSTANT_2 -> constantText(reader.u2());
            case BRANCH_2 -> branchText(pc, reader.s2(), targets);
            case BRANCH_4 -> branchText(pc, reader.s4(), targets);
            case IINC -> "slot " + reader.u1() + " by " + reader.s1();
            case NEWARRAY -> Opcode.arrayTypeName(reader.u1());
            case MULTIANEWARRAY -> constantText(reader.u2()) + " dims " + reader.u1();
            case INVOKE_INTERFACE -> invokeInterfaceText(reader);
            case INVOKE_DYNAMIC -> invokeDynamicText(reader);
            case TABLESWITCH -> tableSwitchText(reader, pc, targets);
            case LOOKUPSWITCH -> lookupSwitchText(reader, pc, targets);
            case WIDE -> "";
        };
        String mnemonic = entry.mnemonic();
        if (entry.operands() == Opcode.Operands.WIDE) {
            wide = true;
            int widened = reader.u1();
            if (!Opcode.isDefined(widened)) {
                throw new ClassFormatException(
                    String.format("undefined widened opcode 0x%02X at pc %d", widened, pc));
            }
            mnemonic = "wide " + Opcode.mnemonic(widened);
            operandText = widened == 0x84
                ? "slot " + reader.u2() + " by " + reader.s2()
                : "slot " + reader.u2();
        }
        int length = reader.position() - pc;
        for (int target : targets) {
            if (target < 0 || target >= codeLength) {
                throw new ClassFormatException(
                    "branch target " + target + " at pc " + pc + " is outside the code array");
            }
        }
        return new Instruction(pc, opcode, mnemonic, length, operandText, List.copyOf(targets), wide);
    }

    private String constantText(int index) {
        if (!pool.validIndex(index)) {
            throw new ClassFormatException("instruction references invalid constant pool index " + index);
        }
        return "#" + index + " " + pool.describe(index);
    }

    private static String branchText(int pc, int offset, List<Integer> targets) {
        int target = pc + offset;
        targets.add(target);
        return "-> " + target;
    }

    private String invokeInterfaceText(ByteReader reader) {
        String target = constantText(reader.u2());
        int count = reader.u1();
        int zero = reader.u1();
        if (zero != 0) {
            throw new ClassFormatException("invokeinterface must be followed by a zero byte");
        }
        return target + " count " + count;
    }

    private String invokeDynamicText(ByteReader reader) {
        String target = constantText(reader.u2());
        if (reader.u2() != 0) {
            throw new ClassFormatException("invokedynamic must be followed by two zero bytes");
        }
        return target;
    }

    private static String tableSwitchText(ByteReader reader, int pc, List<Integer> targets) {
        reader.align(0, 4);
        int defaultTarget = pc + reader.s4();
        int low = reader.s4();
        int high = reader.s4();
        if (low > high) {
            throw new ClassFormatException("tableswitch low " + low + " exceeds high " + high);
        }
        targets.add(defaultTarget);
        long count = (long) high - low + 1;
        for (long i = 0; i < count; i++) {
            targets.add(pc + reader.s4());
        }
        return "range " + low + ".." + high + " default -> " + defaultTarget + " (" + count + " cases)";
    }

    private static String lookupSwitchText(ByteReader reader, int pc, List<Integer> targets) {
        reader.align(0, 4);
        int defaultTarget = pc + reader.s4();
        int pairs = reader.s4();
        if (pairs < 0) {
            throw new ClassFormatException("lookupswitch npairs is negative: " + pairs);
        }
        targets.add(defaultTarget);
        for (int i = 0; i < pairs; i++) {
            reader.s4();
            targets.add(pc + reader.s4());
        }
        return "default -> " + defaultTarget + " (" + pairs + " pairs)";
    }

    public List<String> listing(AttributeInfo.Code code) {
        List<Instruction> instructions = decode(code.code());
        Set<Integer> labels = new LinkedHashSet<>();
        for (Instruction instruction : instructions) {
            labels.addAll(instruction.branchTargets());
        }
        for (AttributeInfo.ExceptionEntry handler : code.exceptionTable()) {
            labels.add(handler.handlerPc());
        }
        List<String> lines = new ArrayList<>();
        for (Instruction instruction : instructions) {
            String marker = labels.contains(instruction.pc()) ? ">" : " ";
            lines.add(String.format("%s %6d: %s", marker, instruction.pc(), instruction.text()));
        }
        return List.copyOf(lines);
    }

    public Set<Integer> instructionBoundaries(byte[] code) {
        Set<Integer> boundaries = new LinkedHashSet<>();
        for (Instruction instruction : decode(code)) {
            boundaries.add(instruction.pc());
        }
        return boundaries;
    }
}
