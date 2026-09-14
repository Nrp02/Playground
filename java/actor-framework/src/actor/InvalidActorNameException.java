package actor;

public final class InvalidActorNameException extends IllegalArgumentException {
    private static final long serialVersionUID = 1L;

    public InvalidActorNameException(String message) {
        super(message);
    }
}
