package classfile;

import java.util.ArrayList;
import java.util.List;

public final class ClassPrinter {
    private final ClassFile classFile;

    public ClassPrinter(ClassFile classFile) {
        this.classFile = classFile;
    }

    public List<String> header() {
        List<String> lines = new ArrayList<>();
        List<String> flags = AccessFlags.forClass(classFile.accessFlags()).stream()
            .filter(flag -> !flag.equals("interface") && !flag.equals("super"))
            .toList();
        String kind = classFile.isInterface() ? "interface" : "class";
        String prefix = flags.isEmpty() ? "" : String.join(" ", flags) + " ";
        lines.add(prefix + kind + " " + classFile.thisClassName());
        lines.add("  class file version " + classFile.majorVersion() + "." + classFile.minorVersion()
            + " (Java " + classFile.javaVersion() + ")");
        classFile.superClassName().ifPresent(name -> lines.add("  extends " + name));
        if (!classFile.interfaceNames().isEmpty()) {
            lines.add("  implements " + String.join(", ", classFile.interfaceNames()));
        }
        classFile.sourceFile().ifPresent(name -> lines.add("  source " + name));
        lines.add("  constant pool entries " + (classFile.constantPool().size() - 1)
            + ", fields " + classFile.fields().size()
            + ", methods " + classFile.methods().size());
        return List.copyOf(lines);
    }

    public List<String> fields() {
        return classFile.fields().stream().map(field -> "  " + field.prettyField()).toList();
    }

    public List<String> methodSignatures() {
        return classFile.methods().stream().map(method -> "  " + method.prettyMethod()).toList();
    }

    public List<String> disassemble(MemberInfo method) {
        List<String> lines = new ArrayList<>();
        lines.add(method.prettyMethod());
        if (method.code().isEmpty()) {
            lines.add("  <no code>");
            return List.copyOf(lines);
        }
        AttributeInfo.Code code = method.code().get();
        lines.add("  max_stack " + code.maxStack() + ", max_locals " + code.maxLocals()
            + ", code length " + code.code().length);
        lines.addAll(new Disassembler(classFile.constantPool()).listing(code));
        for (AttributeInfo.ExceptionEntry handler : code.exceptionTable()) {
            String type = handler.catchType() == 0
                ? "any"
                : classFile.constantPool().className(handler.catchType());
            lines.add("  handler " + handler.startPc() + ".." + handler.endPc()
                + " -> " + handler.handlerPc() + " catch " + type);
        }
        code.lineNumbers().ifPresent(table -> lines.add("  line numbers " + table.lines().size()));
        return List.copyOf(lines);
    }

    public List<String> attributeNames() {
        return classFile.attributes().stream().map(AttributeInfo::name).toList();
    }
}
