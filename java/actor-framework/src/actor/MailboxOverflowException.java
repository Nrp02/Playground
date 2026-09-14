package actor;

public final class MailboxOverflowException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public MailboxOverflowException(String path, int capacity) {
        super("mailbox of " + path + " is full (capacity " + capacity + ")");
    }
}
