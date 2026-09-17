package nio;

public interface Cancellable {
    boolean cancel();

    boolean isCancelled();
}
