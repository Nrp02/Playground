package di;

public class ProvisionException extends DiException {
    private static final long serialVersionUID = 1L;

    public ProvisionException(String message) {
        super(message);
    }

    public ProvisionException(String message, Throwable cause) {
        super(message, cause);
    }
}
