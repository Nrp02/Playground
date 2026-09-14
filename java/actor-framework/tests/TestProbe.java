import actor.Actor;
import actor.ActorRef;
import actor.ActorSystem;
import actor.Props;
import java.time.Duration;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

public final class TestProbe {
    public record Received(Object message, ActorRef sender) {
    }

    private final LinkedBlockingQueue<Received> queue = new LinkedBlockingQueue<>();
    private final ActorRef ref;

    public TestProbe(ActorSystem system) {
        LinkedBlockingQueue<Received> sink = queue;
        ref = system.spawn(Props.create(() -> new ProbeActor(sink)));
    }

    public ActorRef ref() {
        return ref;
    }

    public Received expectReceived() {
        try {
            Received received = queue.poll(3, TimeUnit.SECONDS);
            if (received == null) {
                throw new AssertionError("probe timed out waiting for a message");
            }
            return received;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new AssertionError("interrupted");
        }
    }

    public Object expectMsg() {
        return expectReceived().message();
    }

    public <T> T expectMsgClass(Class<T> type) {
        Object message = expectMsg();
        if (!type.isInstance(message)) {
            throw new AssertionError("expected " + type.getSimpleName() + " but got " + message);
        }
        return type.cast(message);
    }

    public void expectNoMsg(Duration wait) {
        try {
            Received received = queue.poll(wait.toMillis(), TimeUnit.MILLISECONDS);
            if (received != null) {
                throw new AssertionError("expected no message but got " + received.message());
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new AssertionError("interrupted");
        }
    }

    private static final class ProbeActor extends Actor {
        private final LinkedBlockingQueue<Received> sink;

        ProbeActor(LinkedBlockingQueue<Received> sink) {
            this.sink = sink;
        }

        @Override
        public void receive(Object message) {
            sink.add(new Received(message, sender()));
        }
    }
}
