package actor;

public final class Messages {

    private Messages() {
    }

    public record Terminated(ActorRef actor) {
    }

    public record Failure(Throwable cause) {
    }

    public record Broadcast(Object message) {
    }

    public record DeadLetter(Object message, ActorRef sender, ActorRef recipient) {
        @Override
        public String toString() {
            return "DeadLetter(" + Records.describe(message) + " from " + sender.path() + " to " + recipient.path() + ")";
        }
    }

    public record UnhandledMessage(Object message, ActorRef sender, ActorRef recipient) {
    }

    public record ErrorEvent(ActorRef actor, Throwable cause, Object message) {
    }
}
