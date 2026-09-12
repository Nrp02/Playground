package classfile;

import java.util.List;

public record Instruction(int pc, int opcode, String mnemonic, int length, String operandText,
                          List<Integer> branchTargets, boolean wide) {

    public boolean isBranch() {
        return !branchTargets.isEmpty();
    }

    public boolean isReturn() {
        return (opcode >= 0xac && opcode <= 0xb1) || opcode == 0xbf;
    }

    public boolean isUnconditionalJump() {
        return opcode == 0xa7 || opcode == 0xc8;
    }

    public String text() {
        return operandText.isEmpty() ? mnemonic : mnemonic + " " + operandText;
    }

    @Override
    public String toString() {
        return String.format("%6d: %s", pc, text());
    }
}
