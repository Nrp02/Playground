package classfile;

public final class ClassFormatException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public ClassFormatException(String message) {
        super(message);
    }
}
