import actor.Actor;
import actor.ActorRef;
import actor.AskTimeoutException;
import actor.Messages;
import actor.Props;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

public final class MessagingTest {

    private MessagingTest() {
    }

    public static void run() {
        Assertions.suite("messaging");

        Assertions.test("tell preserves per-sender ordering", system -> {
            TestProbe probe = new TestProbe(system);
            ActorRef forwarder = system.spawn(Props.create(() -> new TestActors.Forwarder(probe.ref())), "forwarder");
            for (int i = 0; i < 200; i++) {
                forwarder.tell(i);
            }
            for (int i = 0; i < 200; i++) {
                Assertions.assertEquals("message " + i, i, probe.expectMsg());
            }
        });

        Assertions.test("ask receives the reply", system -> {
            ActorRef echo = system.spawn(Props.create(TestActors.Echo::new), "echo");
            Assertions.assertEquals("echo reply", "hello", Assertions.await(echo.ask("hello", Duration.ofSeconds(1))));
            String typed = Assertions.await(echo.ask("typed", Duration.ofSeconds(1), String.class));
            Assertions.assertEquals("typed reply", "typed", typed);
        });

        Assertions.test("ask times out when no reply arrives", system -> {
            ActorRef silent = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void receive(Object message) {
                }
            }), "silent");
            Throwable failure = Assertions.awaitFailure(silent.ask("anyone?", Duration.ofMillis(50)));
            Assertions.assertTrue("timeout exception", failure instanceof AskTimeoutException);
            Assertions.assertTrue("message names the path", failure.getMessage().contains("/user/silent"));
        });

        Assertions.test("Failure reply completes the ask exceptionally", system -> {
            ActorRef failing = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void receive(Object message) {
                    sender().tell(new Messages.Failure(new IllegalArgumentException("bad " + message)), self());
                }
            }), "failing");
            Throwable failure = Assertions.awaitFailure(failing.ask("input", Duration.ofSeconds(1)));
            Assertions.assertTrue("cause type", failure instanceof IllegalArgumentException);
            Assertions.assertEquals("cause message", "bad input", failure.getMessage());
        });

        Assertions.test("forward keeps the original sender", system -> {
            ActorRef echo = system.spawn(Props.create(TestActors.Echo::new), "echo");
            ActorRef forwarder = system.spawn(Props.create(() -> new TestActors.Forwarder(echo)), "fwd");
            Assertions.assertEquals("reply routed to asker", 42, Assertions.await(forwarder.ask(42, Duration.ofSeconds(1))));
        });

        Assertions.test("replying to a missing sender becomes a dead letter", system -> {
            List<Messages.DeadLetter> letters = new CopyOnWriteArrayList<>();
            system.eventStream().subscribe(Messages.DeadLetter.class, letters::add);
            ActorRef echo = system.spawn(Props.create(TestActors.Echo::new), "echo");
            echo.tell("into the void");
            Assertions.eventually("dead letter published", () -> !letters.isEmpty());
            Assertions.assertEquals("dead letter payload", "into the void", letters.get(0).message());
            Assertions.assertEquals("recipient", "/deadLetters", letters.get(0).recipient().path());
        });

        Assertions.test("messages to a stopped actor are dead letters", system -> {
            ActorRef echo = system.spawn(Props.create(TestActors.Echo::new), "echo");
            system.stop(echo);
            Assertions.await(echo.whenTerminated());
            long before = system.deadLetterCount();
            echo.tell("late");
            Assertions.assertEquals("dead letter count", before + 1, system.deadLetterCount());
        });

        Assertions.test("second reply to an ask is a dead letter", system -> {
            ActorRef twice = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void receive(Object message) {
                    sender().tell("first", self());
                    sender().tell("second", self());
                }
            }), "twice");
            Assertions.assertEquals("first wins", "first", Assertions.await(twice.ask("go", Duration.ofSeconds(1))));
            Assertions.eventually("second is dead-lettered", () -> system.deadLetterCount() == 1);
        });

        Assertions.test("unhandled messages are published", system -> {
            List<Messages.UnhandledMessage> unhandled = new CopyOnWriteArrayList<>();
            system.eventStream().subscribe(Messages.UnhandledMessage.class, unhandled::add);
            ActorRef counter = system.spawn(Props.create(TestActors.Counter::new), "counter");
            counter.tell(3.14);
            Assertions.eventually("unhandled event", () -> unhandled.size() == 1);
            Assertions.assertEquals("payload", 3.14, unhandled.get(0).message());
        });

        Assertions.test("event stream subscriptions can be cancelled", system -> {
            List<Object> seen = new CopyOnWriteArrayList<>();
            var subscription = system.eventStream().subscribe(String.class, seen::add);
            system.eventStream().publish("one");
            Assertions.assertTrue("first cancel", subscription.cancel());
            system.eventStream().publish("two");
            Assertions.assertEquals("only first delivered", List.of("one"), List.copyOf(seen));
            Assertions.assertFalse("second cancel is a no-op", subscription.cancel());
        });
    }
}
