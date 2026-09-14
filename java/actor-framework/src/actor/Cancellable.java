package actor;

public interface Cancellable {
    boolean cancel();

    boolean isCancelled();
}
