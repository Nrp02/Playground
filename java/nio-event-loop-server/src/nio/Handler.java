package nio;

@FunctionalInterface
public interface Handler {
    default void onOpen(Connection connection) {
    }

    void onMessage(Connection connection, Object message);

    default void onWritabilityChanged(Connection connection, boolean writable) {
    }

    default void onClose(Connection connection, Throwable cause) {
    }
}
