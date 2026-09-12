package classfile;

import java.util.List;
import java.util.Optional;

public record MemberInfo(int accessFlags, String name, String descriptor, List<AttributeInfo> attributes) {

    public Optional<AttributeInfo.Code> code() {
        return attributes.stream()
            .filter(AttributeInfo.Code.class::isInstance)
            .map(AttributeInfo.Code.class::cast)
            .findFirst();
    }

    public Optional<String> signature() {
        return attributes.stream()
            .filter(AttributeInfo.Signature.class::isInstance)
            .map(a -> ((AttributeInfo.Signature) a).signature())
            .findFirst();
    }

    public boolean isStatic() {
        return AccessFlags.has(accessFlags, AccessFlags.STATIC);
    }

    public boolean isAbstract() {
        return AccessFlags.has(accessFlags, AccessFlags.ABSTRACT);
    }

    public boolean isNative() {
        return AccessFlags.has(accessFlags, AccessFlags.NATIVE);
    }

    public String prettyField() {
        List<String> flags = AccessFlags.forField(accessFlags);
        String prefix = flags.isEmpty() ? "" : String.join(" ", flags) + " ";
        return prefix + Descriptor.prettyType(descriptor) + " " + name;
    }

    public String prettyMethod() {
        List<String> flags = AccessFlags.forMethod(accessFlags);
        String prefix = flags.isEmpty() ? "" : String.join(" ", flags) + " ";
        return prefix + Descriptor.prettyMethod(name, descriptor);
    }
}
