package actor;

import java.time.Duration;

final class Exceptions {

    private Exceptions() {
    }

    static String timeoutMessage(String path, Duration timeout) {
        return "ask to " + path + " timed out after " + timeout.toMillis() + " ms";
    }
}
