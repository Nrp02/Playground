package classfile;

import java.util.List;
import java.util.Optional;

public sealed interface AttributeInfo {

    String name();

    record ExceptionEntry(int startPc, int endPc, int handlerPc, int catchType) {
    }

    record LineNumber(int startPc, int lineNumber) {
    }

    record LocalVariable(int startPc, int length, String name, String descriptor, int slot) {
    }

    record InnerClassEntry(int innerClassIndex, int outerClassIndex, int innerNameIndex, int flags) {
    }

    record BootstrapMethod(int methodHandleIndex, List<Integer> argumentIndexes) {
    }

    record Code(int maxStack, int maxLocals, byte[] code, List<ExceptionEntry> exceptionTable,
                List<AttributeInfo> attributes) implements AttributeInfo {
        @Override
        public String name() {
            return "Code";
        }

        public Optional<LineNumberTable> lineNumbers() {
            return attributes.stream()
                .filter(LineNumberTable.class::isInstance)
                .map(LineNumberTable.class::cast)
                .findFirst();
        }
    }

    record LineNumberTable(List<LineNumber> lines) implements AttributeInfo {
        @Override
        public String name() {
            return "LineNumberTable";
        }

        public Optional<Integer> lineFor(int pc) {
            int best = -1;
            for (LineNumber entry : lines) {
                if (entry.startPc() <= pc) {
                    best = entry.lineNumber();
                }
            }
            return best < 0 ? Optional.empty() : Optional.of(best);
        }
    }

    record LocalVariableTable(List<LocalVariable> variables) implements AttributeInfo {
        @Override
        public String name() {
            return "LocalVariableTable";
        }
    }

    record SourceFile(String fileName) implements AttributeInfo {
        @Override
        public String name() {
            return "SourceFile";
        }
    }

    record ConstantValue(int index) implements AttributeInfo {
        @Override
        public String name() {
            return "ConstantValue";
        }
    }

    record Exceptions(List<Integer> exceptionClassIndexes) implements AttributeInfo {
        @Override
        public String name() {
            return "Exceptions";
        }
    }

    record Signature(String signature) implements AttributeInfo {
        @Override
        public String name() {
            return "Signature";
        }
    }

    record InnerClasses(List<InnerClassEntry> classes) implements AttributeInfo {
        @Override
        public String name() {
            return "InnerClasses";
        }
    }

    record BootstrapMethods(List<BootstrapMethod> methods) implements AttributeInfo {
        @Override
        public String name() {
            return "BootstrapMethods";
        }
    }

    record Deprecated() implements AttributeInfo {
        @Override
        public String name() {
            return "Deprecated";
        }
    }

    record Synthetic() implements AttributeInfo {
        @Override
        public String name() {
            return "Synthetic";
        }
    }

    record Raw(String attributeName, byte[] data) implements AttributeInfo {
        @Override
        public String name() {
            return attributeName;
        }
    }
}
