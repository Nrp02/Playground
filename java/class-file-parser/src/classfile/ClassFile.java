package classfile;

import java.util.List;
import java.util.Optional;

public record ClassFile(int minorVersion, int majorVersion, ConstantPool constantPool, int accessFlags,
                        int thisClassIndex, int superClassIndex, List<Integer> interfaceIndexes,
                        List<MemberInfo> fields, List<MemberInfo> methods, List<AttributeInfo> attributes) {

    public String thisClassName() {
        return constantPool.className(thisClassIndex);
    }

    public Optional<String> superClassName() {
        return superClassIndex == 0 ? Optional.empty() : Optional.of(constantPool.className(superClassIndex));
    }

    public List<String> interfaceNames() {
        return interfaceIndexes.stream().map(constantPool::className).toList();
    }

    public Optional<String> sourceFile() {
        return attributes.stream()
            .filter(AttributeInfo.SourceFile.class::isInstance)
            .map(a -> ((AttributeInfo.SourceFile) a).fileName())
            .findFirst();
    }

    public boolean isInterface() {
        return AccessFlags.has(accessFlags, AccessFlags.INTERFACE);
    }

    public String javaVersion() {
        return switch (majorVersion) {
            case 45 -> "1.1";
            case 46 -> "1.2";
            case 47 -> "1.3";
            case 48 -> "1.4";
            case 49 -> "5";
            case 50 -> "6";
            case 51 -> "7";
            case 52 -> "8";
            default -> majorVersion >= 53 ? Integer.toString(majorVersion - 44) : "unknown";
        };
    }

    public Optional<MemberInfo> findMethod(String name) {
        return methods.stream().filter(m -> m.name().equals(name)).findFirst();
    }
}
