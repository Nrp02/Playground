package actor;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

final class DeadLetterRef extends ActorRef {
    private final ActorSystem system;
    private final CompletableFuture<Void> never = new CompletableFuture<>();

    DeadLetterRef(ActorSystem system) {
        this.system = system;
    }

    @Override
    public String path() {
        return "/deadLetters";
    }

    @Override
    public void tell(Object message, ActorRef sender) {
        Objects.requireNonNull(message, "message");
        system.publishDeadLetter(message, sender == null ? this : sender, this);
    }

    @Override
    public CompletableFuture<Void> whenTerminated() {
        return never.copy();
    }

    @Override
    ActorSystem system() {
        return system;
    }
}
