import actor.Actor;
import actor.ActorRef;
import actor.Cancellable;
import actor.Messages;
import actor.Props;
import actor.Routers;
import actor.Signals;
import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class RouterSchedulerTest {

    private RouterSchedulerTest() {
    }

    static final class WhoAmI extends Actor {
        @Override
        public void receive(Object message) {
            sender().tell(self().path(), self());
        }
    }

    static final class Ticker extends Actor {
        private final AtomicInteger ticks;

        Ticker(AtomicInteger ticks) {
            this.ticks = ticks;
        }

        @Override
        public void receive(Object message) {
            ticks.incrementAndGet();
        }
    }

    public static void run() {
        Assertions.suite("routers and scheduler");

        Assertions.test("round robin spreads messages evenly", system -> {
            ActorRef router = system.spawn(Routers.roundRobinPool(4, Props.create(WhoAmI::new)), "pool");
            TestProbe probe = new TestProbe(system);
            for (int i = 0; i < 40; i++) {
                router.tell(i, probe.ref());
            }
            Map<Object, Integer> counts = new HashMap<>();
            for (int i = 0; i < 40; i++) {
                counts.merge(probe.expectMsg(), 1, Integer::sum);
            }
            Assertions.assertEquals("routee count", 4, counts.size());
            Assertions.assertTrue("even split", counts.values().stream().allMatch(count -> count == 10));
            Assertions.assertTrue("routee paths", counts.keySet().contains("/user/pool/routee-0"));
        });

        Assertions.test("broadcast pool reaches every routee", system -> {
            ActorRef router = system.spawn(Routers.broadcastPool(3, Props.create(WhoAmI::new)), "fanout");
            TestProbe probe = new TestProbe(system);
            router.tell("hello", probe.ref());
            List<Object> replies = List.of(probe.expectMsg(), probe.expectMsg(), probe.expectMsg());
            Assertions.assertEquals("distinct routees", 3L, replies.stream().distinct().count());
            probe.expectNoMsg(Duration.ofMillis(50));
        });

        Assertions.test("Broadcast envelope works on any router", system -> {
            ActorRef router = system.spawn(Routers.roundRobinPool(2, Props.create(WhoAmI::new)), "rr");
            TestProbe probe = new TestProbe(system);
            router.tell(new Messages.Broadcast("all"), probe.ref());
            Assertions.assertFalse("two replies", probe.expectMsg().equals(probe.expectMsg()));
        });

        Assertions.test("router stops once all routees are gone", system -> {
            ActorRef router = system.spawn(Routers.roundRobinPool(2, Props.create(WhoAmI::new)), "shrinking");
            Object routees = Assertions.await(router.ask(Signals.GET_ROUTEES, Duration.ofSeconds(1)));
            List<?> list = (List<?>) routees;
            Assertions.assertEquals("initial size", 2, list.size());
            ((ActorRef) list.get(0)).tell(Signals.POISON_PILL);
            Assertions.eventually("one routee left", () -> {
                try {
                    return ((List<?>) Assertions.await(router.ask(Signals.GET_ROUTEES, Duration.ofSeconds(1)))).size() == 1;
                } catch (Exception e) {
                    return false;
                }
            });
            ((ActorRef) list.get(1)).tell(Signals.POISON_PILL);
            Assertions.await(router.whenTerminated());
        });

        Assertions.test("smallest mailbox avoids a busy routee", system -> {
            CountDownLatch started = new CountDownLatch(1);
            CountDownLatch release = new CountDownLatch(1);
            List<Object> seen = new CopyOnWriteArrayList<>();
            AtomicInteger instance = new AtomicInteger();
            ActorRef router = system.spawn(Routers.smallestMailboxPool(2, Props.create(() -> instance.getAndIncrement() == 0
                ? new TestActors.Gate(started, release, seen)
                : new TestActors.Gate(new CountDownLatch(1), release, seen))), "smallest");
            router.tell("hold");
            Assertions.assertTrue("first routee busy", started.await(3, TimeUnit.SECONDS));
            for (int i = 0; i < 5; i++) {
                String message = "m" + i;
                router.tell(message);
                Assertions.eventually("idle routee handled " + message, () -> seen.contains(message));
            }
            Assertions.assertEquals("busy routee received nothing more", 5, seen.size());
            release.countDown();
        });

        Assertions.test("scheduleOnce delivers after the delay", system -> {
            TestProbe probe = new TestProbe(system);
            long start = System.nanoTime();
            system.scheduler().scheduleOnce(Duration.ofMillis(60), probe.ref(), "tick");
            Assertions.assertEquals("delivered", "tick", probe.expectMsg());
            Assertions.assertTrue("not early", System.nanoTime() - start >= Duration.ofMillis(55).toNanos());
        });

        Assertions.test("cancelled timers never fire", system -> {
            TestProbe probe = new TestProbe(system);
            Cancellable timer = system.scheduler().scheduleOnce(Duration.ofMillis(80), probe.ref(), "tick");
            Assertions.assertTrue("cancelled", timer.cancel());
            Assertions.assertTrue("reports cancelled", timer.isCancelled());
            probe.expectNoMsg(Duration.ofMillis(150));
        });

        Assertions.test("fixed-rate timers stop when the target dies", system -> {
            AtomicInteger ticks = new AtomicInteger();
            ActorRef ticker = system.spawn(Props.create(() -> new Ticker(ticks)), "ticker");
            Cancellable timer = system.scheduler().scheduleAtFixedRate(Duration.ZERO, Duration.ofMillis(10), ticker, "t");
            Assertions.eventually("several ticks", () -> ticks.get() >= 3);
            system.stop(ticker);
            Assertions.await(ticker.whenTerminated());
            Assertions.eventually("timer cancelled", timer::isCancelled);
        });
    }
}
