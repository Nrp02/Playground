package actor;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

final class PromiseRef extends ActorRef {
    private final ActorSystem system;
    private final String path;
    private final CompletableFuture<Object> future = new CompletableFuture<>();

    PromiseRef(ActorSystem system, String path) {
        this.system = system;
        this.path = path;
    }

    CompletableFuture<Object> future() {
        return future;
    }

    @Override
    public String path() {
        return path;
    }

    @Override
    public void tell(Object message, ActorRef sender) {
        Objects.requireNonNull(message, "message");
        boolean completed = message instanceof Messages.Failure failure
            ? future.completeExceptionally(failure.cause())
            : future.complete(message);
        if (!completed) {
            system.publishDeadLetter(message, sender == null ? system.deadLetters() : sender, this);
        }
    }

    @Override
    public CompletableFuture<Void> whenTerminated() {
        return future.<Void>handle((value, error) -> null);
    }

    @Override
    ActorSystem system() {
        return system;
    }
}
