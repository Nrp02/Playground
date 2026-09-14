package di;

public abstract class DiException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    protected DiException(String message) {
        super(message);
    }

    protected DiException(String message, Throwable cause) {
        super(message, cause);
    }
}
