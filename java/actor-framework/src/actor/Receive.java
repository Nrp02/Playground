package actor;

@FunctionalInterface
public interface Receive {
    void onMessage(Object message) throws Exception;
}
