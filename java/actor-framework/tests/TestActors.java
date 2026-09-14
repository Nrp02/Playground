import actor.Actor;
import actor.ActorRef;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class TestActors {

    private TestActors() {
    }

    public static final class Echo extends Actor {
        @Override
        public void receive(Object message) {
            sender().tell(message, self());
        }
    }

    public static final class Forwarder extends Actor {
        private final ActorRef target;

        public Forwarder(ActorRef target) {
            this.target = target;
        }

        @Override
        public void receive(Object message) {
            target.forward(message, context());
        }
    }

    public static final class Counter extends Actor {
        private int count;

        @Override
        public void receive(Object message) throws Exception {
            switch (String.valueOf(message)) {
                case "inc" -> count++;
                case "get" -> sender().tell(count, self());
                case "boom" -> throw new IllegalStateException("boom at " + count);
                case "fatal" -> throw new AssertionError("fatal");
                default -> unhandled(message);
            }
        }
    }

    public static final class Recorder extends Actor {
        private final String label;
        private final List<String> log;

        public Recorder(String label, List<String> log) {
            this.label = label;
            this.log = log;
        }

        @Override
        public void preStart() {
            log.add(label + ":preStart");
        }

        @Override
        public void postStop() {
            log.add(label + ":postStop");
        }

        @Override
        public void preRestart(Throwable reason, Object message) {
            log.add(label + ":preRestart:" + reason.getMessage() + ":" + message);
        }

        @Override
        public void postRestart(Throwable reason) {
            log.add(label + ":postRestart:" + reason.getMessage());
        }

        @Override
        public void receive(Object message) throws Exception {
            if (message instanceof String text && text.startsWith("fail:")) {
                throw new IllegalStateException(text.substring(5));
            }
            if (message instanceof Spawn spawn) {
                context().spawn(actor.Props.create(() -> new Recorder(spawn.label(), log)), spawn.name());
                sender().tell("spawned", self());
                return;
            }
            log.add(label + ":" + message);
            sender().tell(message, self());
        }
    }

    public record Spawn(String name, String label) {
    }

    public static final class Gate extends Actor {
        private final CountDownLatch started;
        private final CountDownLatch release;
        private final List<Object> seen;

        public Gate(CountDownLatch started, CountDownLatch release, List<Object> seen) {
            this.started = started;
            this.release = release;
            this.seen = seen;
        }

        @Override
        public void receive(Object message) throws InterruptedException {
            if ("hold".equals(message)) {
                started.countDown();
                if (!release.await(5, TimeUnit.SECONDS)) {
                    throw new IllegalStateException("gate was never released");
                }
                return;
            }
            seen.add(message);
            if ("done".equals(message)) {
                sender().tell("done", self());
            }
        }
    }
}
