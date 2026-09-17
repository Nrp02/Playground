package nio;

import java.io.IOException;

public final class ConnectionTimeoutException extends IOException {
    private static final long serialVersionUID = 1L;

    public ConnectionTimeoutException(String message) {
        super(message);
    }
}
