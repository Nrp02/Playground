package actor;

import java.time.Duration;

public final class AskTimeoutException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public AskTimeoutException(String path, Duration timeout) {
        super(Exceptions.timeoutMessage(path, timeout));
    }
}
