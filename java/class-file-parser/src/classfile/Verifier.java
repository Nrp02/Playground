package classfile;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class Verifier {
    public static final int MIN_MAJOR_VERSION = 45;
    public static final int MAX_MAJOR_VERSION = 70;

    private final ClassFile classFile;
    private final List<String> problems = new ArrayList<>();

    public Verifier(ClassFile classFile) {
        this.classFile = classFile;
    }

    public static List<String> verify(ClassFile classFile) {
        return new Verifier(classFile).run();
    }

    public List<String> run() {
        problems.clear();
        checkVersion();
        checkClassStructure();
        checkFlags();
        checkMembers();
        return List.copyOf(problems);
    }

    private void report(String problem) {
        problems.add(problem);
    }

    private void checkVersion() {
        int major = classFile.majorVersion();
        if (major < MIN_MAJOR_VERSION || major > MAX_MAJOR_VERSION) {
            report("unsupported major version " + major + " (expected "
                + MIN_MAJOR_VERSION + ".." + MAX_MAJOR_VERSION + ")");
        }
        if (major < 56 && classFile.minorVersion() == 65535) {
            report("preview minor version 65535 is only legal from major version 56");
        }
    }

    private void checkClassStructure() {
        ConstantPool pool = classFile.constantPool();
        if (!pool.validIndex(classFile.thisClassIndex())) {
            report("this_class index " + classFile.thisClassIndex() + " is not a live constant pool entry");
        } else if (!(pool.at(classFile.thisClassIndex()) instanceof Constant.ClassRef)) {
            report("this_class must point to a Class entry");
        }
        int superIndex = classFile.superClassIndex();
        if (superIndex != 0) {
            if (!pool.validIndex(superIndex)) {
                report("super_class index " + superIndex + " is not a live constant pool entry");
            } else if (!(pool.at(superIndex) instanceof Constant.ClassRef)) {
                report("super_class must point to a Class entry");
            }
        } else if (!"java.lang.Object".equals(safeName())) {
            report("only java.lang.Object may have super_class 0");
        }
        if (classFile.isInterface() && superIndex != 0
            && !"java.lang.Object".equals(pool.className(superIndex))) {
            report("an interface must extend java.lang.Object directly");
        }
        for (int index : classFile.interfaceIndexes()) {
            if (!pool.validIndex(index) || !(pool.at(index) instanceof Constant.ClassRef)) {
                report("interface index " + index + " does not point to a Class entry");
            }
        }
    }

    private String safeName() {
        try {
            return classFile.thisClassName();
        } catch (ClassFormatException ignored) {
            return "";
        }
    }

    private void checkFlags() {
        int flags = classFile.accessFlags();
        if (AccessFlags.has(flags, AccessFlags.FINAL) && AccessFlags.has(flags, AccessFlags.ABSTRACT)) {
            report("class is both final and abstract");
        }
        if (classFile.isInterface()) {
            if (!AccessFlags.has(flags, AccessFlags.ABSTRACT)) {
                report("interface must be abstract");
            }
            if (AccessFlags.has(flags, AccessFlags.FINAL)) {
                report("interface must not be final");
            }
        } else if (AccessFlags.has(flags, AccessFlags.ANNOTATION)) {
            report("annotation flag requires the interface flag");
        }
    }

    private void checkMembers() {
        checkDuplicates("field", classFile.fields());
        checkDuplicates("method", classFile.methods());
        for (MemberInfo field : classFile.fields()) {
            checkDescriptor("field " + field.name(), field.descriptor(), false);
        }
        for (MemberInfo method : classFile.methods()) {
            checkDescriptor("method " + method.name(), method.descriptor(), true);
            checkMethodFlags(method);
            checkCode(method);
        }
    }

    private void checkDuplicates(String kind, List<MemberInfo> members) {
        Set<String> seen = new HashSet<>();
        for (MemberInfo member : members) {
            String key = member.name() + ":" + member.descriptor();
            if (!seen.add(key)) {
                report("duplicate " + kind + " " + key);
            }
        }
    }

    private void checkDescriptor(String label, String descriptor, boolean method) {
        try {
            if (method) {
                Descriptor.parseMethod(descriptor);
            } else {
                Descriptor.prettyType(descriptor);
            }
        } catch (ClassFormatException e) {
            report(label + " has an invalid descriptor: " + e.getMessage());
        }
    }

    private void checkMethodFlags(MemberInfo method) {
        boolean abstractMethod = method.isAbstract();
        boolean hasCode = method.code().isPresent();
        if (abstractMethod && hasCode) {
            report("abstract method " + method.name() + " must not have a Code attribute");
        }
        if (!abstractMethod && !method.isNative() && !hasCode) {
            report("method " + method.name() + " has neither Code nor abstract/native flags");
        }
        if (abstractMethod && AccessFlags.has(method.accessFlags(),
            AccessFlags.FINAL | AccessFlags.STATIC | AccessFlags.PRIVATE | AccessFlags.SYNCHRONIZED)) {
            report("abstract method " + method.name() + " has an incompatible flag");
        }
    }

    private void checkCode(MemberInfo method) {
        if (method.code().isEmpty()) {
            return;
        }
        AttributeInfo.Code code = method.code().get();
        String label = "method " + method.name();
        Set<Integer> boundaries;
        List<Instruction> instructions;
        try {
            Disassembler disassembler = new Disassembler(classFile.constantPool());
            instructions = disassembler.decode(code.code());
            boundaries = disassembler.instructionBoundaries(code.code());
        } catch (ClassFormatException e) {
            report(label + " has undecodable bytecode: " + e.getMessage());
            return;
        }
        for (Instruction instruction : instructions) {
            for (int target : instruction.branchTargets()) {
                if (!boundaries.contains(target)) {
                    report(label + " branch at pc " + instruction.pc()
                        + " targets " + target + ", which is not an instruction boundary");
                }
            }
        }
        if (!instructions.isEmpty()) {
            Instruction last = instructions.get(instructions.size() - 1);
            if (!last.isReturn() && !last.isUnconditionalJump()) {
                report(label + " falls off the end of its code array");
            }
        }
        checkLocals(label, code, method, instructions);
        checkExceptionTable(label, code, boundaries);
    }

    private void checkLocals(String label, AttributeInfo.Code code, MemberInfo method,
                             List<Instruction> instructions) {
        int required = Descriptor.parseMethod(method.descriptor()).parameterSlots()
            + (method.isStatic() ? 0 : 1);
        if (code.maxLocals() < required) {
            report(label + " declares max_locals " + code.maxLocals()
                + " but its arguments need " + required);
        }
        if (code.maxStack() == 0 && instructions.stream().anyMatch(i -> !i.isReturn())) {
            report(label + " declares max_stack 0 but contains value-producing instructions");
        }
    }

    private void checkExceptionTable(String label, AttributeInfo.Code code, Set<Integer> boundaries) {
        int codeLength = code.code().length;
        for (AttributeInfo.ExceptionEntry handler : code.exceptionTable()) {
            if (handler.startPc() >= handler.endPc()) {
                report(label + " exception range " + handler.startPc() + ".." + handler.endPc()
                    + " is empty or inverted");
            }
            if (!boundaries.contains(handler.startPc())) {
                report(label + " exception start_pc " + handler.startPc()
                    + " is not an instruction boundary");
            }
            if (handler.endPc() != codeLength && !boundaries.contains(handler.endPc())) {
                report(label + " exception end_pc " + handler.endPc()
                    + " is not an instruction boundary");
            }
            if (!boundaries.contains(handler.handlerPc())) {
                report(label + " handler_pc " + handler.handlerPc()
                    + " is not an instruction boundary");
            }
            if (handler.catchType() != 0
                && !(classFile.constantPool().validIndex(handler.catchType())
                    && classFile.constantPool().at(handler.catchType()) instanceof Constant.ClassRef)) {
                report(label + " catch_type " + handler.catchType() + " is not a Class entry");
            }
        }
    }
}
