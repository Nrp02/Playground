import actor.ActorRef;
import actor.InvalidActorNameException;
import actor.Messages;
import actor.Props;
import actor.Signals;
import actor.SupervisorStrategy;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

public final class LifecycleTest {

    private LifecycleTest() {
    }

    public static void run() {
        Assertions.suite("lifecycle");

        Assertions.test("preStart runs before messages and postStop after stop", system -> {
            List<String> log = new CopyOnWriteArrayList<>();
            ActorRef recorder = system.spawn(Props.create(() -> new TestActors.Recorder("a", log)), "rec");
            Assertions.await(recorder.ask("hi", Duration.ofSeconds(1)));
            system.stop(recorder);
            Assertions.await(recorder.whenTerminated());
            Assertions.assertEquals("hook order", List.of("a:preStart", "a:hi", "a:postStop"), List.copyOf(log));
        });

        Assertions.test("PoisonPill is processed in mailbox order", system -> {
            List<String> log = new CopyOnWriteArrayList<>();
            ActorRef recorder = system.spawn(Props.create(() -> new TestActors.Recorder("p", log)), "rec");
            recorder.tell("one");
            recorder.tell("two");
            recorder.tell(Signals.POISON_PILL);
            recorder.tell("three");
            Assertions.await(recorder.whenTerminated());
            Assertions.assertEquals("messages before the pill", List.of("p:preStart", "p:one", "p:two", "p:postStop"),
                List.copyOf(log));
            Assertions.eventually("three is a dead letter", () -> system.deadLetterCount() >= 1);
        });

        Assertions.test("children stop before their parent", system -> {
            List<String> log = new CopyOnWriteArrayList<>();
            ActorRef parent = system.spawn(Props.create(() -> new TestActors.Recorder("parent", log)), "parent");
            Assertions.await(parent.ask(new TestActors.Spawn("kid", "child"), Duration.ofSeconds(1)));
            ActorRef child = system.lookup("/user/parent/kid").orElseThrow();
            Assertions.await(child.ask(new TestActors.Spawn("grand", "grandchild"), Duration.ofSeconds(1)));
            system.stop(parent);
            Assertions.await(parent.whenTerminated());
            List<String> stops = log.stream().filter(entry -> entry.endsWith("postStop")).toList();
            Assertions.assertEquals("bottom-up stop order",
                List.of("grandchild:postStop", "child:postStop", "parent:postStop"), stops);
            Assertions.assertTrue("child terminated", child.isTerminated());
        });

        Assertions.test("actor names are validated and unique", system -> {
            system.spawn(Props.create(TestActors.Echo::new), "dup");
            Assertions.assertThrows("duplicate", InvalidActorNameException.class,
                () -> system.spawn(Props.create(TestActors.Echo::new), "dup"));
            Assertions.assertThrows("reserved prefix", InvalidActorNameException.class,
                () -> system.spawn(Props.create(TestActors.Echo::new), "$nope"));
            Assertions.assertThrows("slash", InvalidActorNameException.class,
                () -> system.spawn(Props.create(TestActors.Echo::new), "a/b"));
            ActorRef anonymous = system.spawn(Props.create(TestActors.Echo::new));
            Assertions.assertTrue("generated name", anonymous.path().startsWith("/user/$"));
        });

        Assertions.test("lookup finds live actors and forgets terminated ones", system -> {
            ActorRef echo = system.spawn(Props.create(TestActors.Echo::new), "findme");
            Assertions.assertEquals("found", echo, system.lookup("/user/findme").orElseThrow());
            system.stop(echo);
            Assertions.await(echo.whenTerminated());
            Assertions.assertTrue("gone", system.lookup("/user/findme").isEmpty());
        });

        Assertions.test("a name can be reused after termination", system -> {
            ActorRef first = system.spawn(Props.create(TestActors.Echo::new), "reuse");
            system.stop(first);
            Assertions.await(first.whenTerminated());
            Assertions.eventually("name released", () -> {
                try {
                    system.spawn(Props.create(TestActors.Echo::new), "reuse");
                    return true;
                } catch (InvalidActorNameException e) {
                    return false;
                }
            });
        });

        Assertions.test("factories must return fresh instances", system -> {
            TestActors.Echo shared = new TestActors.Echo();
            List<Object> errors = new CopyOnWriteArrayList<>();
            system.eventStream().subscribe(Messages.ErrorEvent.class, event -> errors.add(event.cause()));
            ActorRef first = system.spawn(Props.create(() -> shared), "first");
            Assertions.await(first.ask("ok", Duration.ofSeconds(1)));
            ActorRef second = system.spawn(Props.create(() -> shared)
                .withSupervisorStrategy(SupervisorStrategy.stoppingStrategy()), "second");
            Assertions.eventually("reuse reported", () -> !errors.isEmpty());
            Assertions.assertTrue("illegal state", errors.get(0) instanceof IllegalStateException);
            Assertions.assertFalse("first still alive", first.isTerminated());
            Assertions.assertTrue("second is unusable", second.path().endsWith("second"));
        });

        Assertions.test("graceful shutdown stops every actor", system -> {
            List<String> log = new CopyOnWriteArrayList<>();
            for (int i = 0; i < 5; i++) {
                String label = "r" + i;
                ActorRef ref = system.spawn(Props.create(() -> new TestActors.Recorder(label, log)), label);
                Assertions.await(ref.ask(new TestActors.Spawn("c", label + "c"), Duration.ofSeconds(1)));
            }
            Assertions.assertEquals("live actors", 10, system.liveActorCount());
            Assertions.await(system.terminate());
            Assertions.assertTrue("terminated", system.isTerminated());
            Assertions.assertEquals("postStop count", 10L,
                log.stream().filter(entry -> entry.endsWith("postStop")).count());
            Assertions.assertThrows("spawn after shutdown", IllegalStateException.class,
                () -> system.spawn(Props.create(TestActors.Echo::new), "late"));
        });
    }
}
