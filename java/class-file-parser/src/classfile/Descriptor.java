package classfile;

import java.util.ArrayList;
import java.util.List;

public final class Descriptor {

    private Descriptor() {
    }

    public record MethodSignature(List<String> parameterTypes, String returnType) {
        public int parameterSlots() {
            int slots = 0;
            for (String type : parameterTypes) {
                slots += (type.equals("long") || type.equals("double")) ? 2 : 1;
            }
            return slots;
        }
    }

    public static String prettyType(String descriptor) {
        Cursor cursor = new Cursor(descriptor, 0);
        String type = readType(cursor);
        if (cursor.index != descriptor.length()) {
            throw new ClassFormatException("trailing characters in descriptor: " + descriptor);
        }
        return type;
    }

    public static MethodSignature parseMethod(String descriptor) {
        if (descriptor.isEmpty() || descriptor.charAt(0) != '(') {
            throw new ClassFormatException("method descriptor must start with '(': " + descriptor);
        }
        Cursor cursor = new Cursor(descriptor, 1);
        List<String> parameters = new ArrayList<>();
        while (cursor.index < descriptor.length() && descriptor.charAt(cursor.index) != ')') {
            parameters.add(readType(cursor));
        }
        if (cursor.index >= descriptor.length()) {
            throw new ClassFormatException("method descriptor missing ')': " + descriptor);
        }
        cursor.index++;
        String returnType = readType(cursor);
        if (cursor.index != descriptor.length()) {
            throw new ClassFormatException("trailing characters in descriptor: " + descriptor);
        }
        return new MethodSignature(List.copyOf(parameters), returnType);
    }

    public static String prettyMethod(String name, String descriptor) {
        MethodSignature signature = parseMethod(descriptor);
        return signature.returnType() + " " + name + "(" + String.join(", ", signature.parameterTypes()) + ")";
    }

    private static final class Cursor {
        private final String text;
        private int index;

        private Cursor(String text, int index) {
            this.text = text;
            this.index = index;
        }
    }

    private static String readType(Cursor cursor) {
        if (cursor.index >= cursor.text.length()) {
            throw new ClassFormatException("descriptor ended early: " + cursor.text);
        }
        char c = cursor.text.charAt(cursor.index++);
        return switch (c) {
            case 'B' -> "byte";
            case 'C' -> "char";
            case 'D' -> "double";
            case 'F' -> "float";
            case 'I' -> "int";
            case 'J' -> "long";
            case 'S' -> "short";
            case 'Z' -> "boolean";
            case 'V' -> "void";
            case '[' -> readType(cursor) + "[]";
            case 'L' -> readObjectType(cursor);
            default -> throw new ClassFormatException(
                "unknown descriptor character '" + c + "' in " + cursor.text);
        };
    }

    private static String readObjectType(Cursor cursor) {
        int end = cursor.text.indexOf(';', cursor.index);
        if (end < 0) {
            throw new ClassFormatException("unterminated object descriptor: " + cursor.text);
        }
        String internal = cursor.text.substring(cursor.index, end);
        if (internal.isEmpty()) {
            throw new ClassFormatException("empty object descriptor: " + cursor.text);
        }
        cursor.index = end + 1;
        return internal.replace('/', '.');
    }
}
