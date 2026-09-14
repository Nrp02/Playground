package actor;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;

final class LocalActorRef extends ActorRef {
    private final ActorCell cell;

    LocalActorRef(ActorCell cell) {
        this.cell = cell;
    }

    ActorCell cell() {
        return cell;
    }

    @Override
    public String path() {
        return cell.path();
    }

    @Override
    public void tell(Object message, ActorRef sender) {
        Objects.requireNonNull(message, "message");
        cell.sendUser(new Envelope(message, sender == null ? cell.system().deadLetters() : sender));
    }

    @Override
    public CompletableFuture<Void> whenTerminated() {
        return cell.terminationFuture().copy();
    }

    @Override
    ActorSystem system() {
        return cell.system();
    }
}
