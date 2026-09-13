package stream;

public final class CheckpointException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public CheckpointException(String message) {
        super(message);
    }

    public CheckpointException(String message, Throwable cause) {
        super(message, cause);
    }
}
