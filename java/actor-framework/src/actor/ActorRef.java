package actor;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;

public abstract class ActorRef {

    ActorRef() {
    }

    public abstract String path();

    public abstract void tell(Object message, ActorRef sender);

    public abstract CompletableFuture<Void> whenTerminated();

    abstract ActorSystem system();

    public final void tell(Object message) {
        tell(message, null);
    }

    public final void forward(Object message, ActorContext context) {
        tell(message, context.sender());
    }

    public final CompletableFuture<Object> ask(Object message, Duration timeout) {
        return system().ask(this, message, timeout);
    }

    public final <T> CompletableFuture<T> ask(Object message, Duration timeout, Class<T> type) {
        return ask(message, timeout).thenApply(type::cast);
    }

    public final boolean isTerminated() {
        return whenTerminated().isDone();
    }

    @Override
    public String toString() {
        return "ActorRef(" + path() + ")";
    }
}
