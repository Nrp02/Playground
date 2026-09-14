import actor.Actor;
import actor.ActorRef;
import actor.Props;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public final class ConcurrencyTest {

    private ConcurrencyTest() {
    }

    static final class Exclusive extends Actor {
        private final AtomicInteger inFlight;
        private final AtomicBoolean overlap;
        private long total;

        Exclusive(AtomicInteger inFlight, AtomicBoolean overlap) {
            this.inFlight = inFlight;
            this.overlap = overlap;
        }

        @Override
        public void receive(Object message) {
            if (inFlight.incrementAndGet() != 1) {
                overlap.set(true);
            }
            if (message instanceof Integer value) {
                total += value;
            } else {
                sender().tell(total, self());
            }
            inFlight.decrementAndGet();
        }
    }

    record Ping(int remaining, ActorRef replyTo) {
    }

    static final class Relay extends Actor {
        private ActorRef next;

        @Override
        public void receive(Object message) {
            if (message instanceof ActorRef ref) {
                next = ref;
            } else if (message instanceof Ping ping) {
                if (ping.remaining() == 0) {
                    ping.replyTo().tell("arrived", self());
                } else {
                    next.tell(new Ping(ping.remaining() - 1, ping.replyTo()), self());
                }
            }
        }
    }

    public static void run() {
        Assertions.suite("concurrency");

        Assertions.test("concurrent senders never run an actor on two threads", system -> {
            AtomicInteger inFlight = new AtomicInteger();
            AtomicBoolean overlap = new AtomicBoolean();
            ActorRef exclusive = system.spawn(Props.create(() -> new Exclusive(inFlight, overlap)), "exclusive");
            List<CompletableFuture<Void>> senders = new ArrayList<>();
            for (int t = 0; t < 8; t++) {
                senders.add(CompletableFuture.runAsync(() -> {
                    for (int i = 0; i < 10_000; i++) {
                        exclusive.tell(1);
                    }
                }));
            }
            CompletableFuture.allOf(senders.toArray(CompletableFuture[]::new)).get();
            Object total = Assertions.await(exclusive.ask("total", Duration.ofSeconds(3)));
            Assertions.assertEquals("every message counted", 80_000L, total);
            Assertions.assertFalse("no overlapping invocations", overlap.get());
        });

        Assertions.test("a ring of actors passes a token around", system -> {
            int size = 500;
            List<ActorRef> ring = new ArrayList<>();
            for (int i = 0; i < size; i++) {
                ring.add(system.spawn(Props.create(Relay::new), "relay-" + i));
            }
            for (int i = 0; i < size; i++) {
                ring.get(i).tell(ring.get((i + 1) % size));
            }
            TestProbe probe = new TestProbe(system);
            ring.get(0).tell(new Ping(size * 4, probe.ref()));
            Assertions.assertEquals("token arrived", "arrived", probe.expectMsg());
            Assertions.assertEquals("live actors", size + 1, system.liveActorCount());
        });

        Assertions.test("many parallel asks all complete", system -> {
            ActorRef echo = system.spawn(Props.create(TestActors.Echo::new), "echo");
            List<CompletableFuture<Object>> asks = new ArrayList<>();
            for (int i = 0; i < 2_000; i++) {
                asks.add(echo.ask(i, Duration.ofSeconds(3)));
            }
            for (int i = 0; i < asks.size(); i++) {
                Assertions.assertEquals("reply " + i, i, Assertions.await(asks.get(i)));
            }
        });

        Assertions.test("terminate during heavy traffic still completes", system -> {
            List<ActorRef> counters = new ArrayList<>();
            for (int i = 0; i < 50; i++) {
                counters.add(system.spawn(Props.create(TestActors.Counter::new), "c" + i));
            }
            CompletableFuture<Void> traffic = CompletableFuture.runAsync(() -> {
                for (int round = 0; round < 2_000; round++) {
                    for (ActorRef counter : counters) {
                        counter.tell("inc");
                    }
                }
            });
            Thread.sleep(5);
            Assertions.await(system.terminate());
            traffic.get();
            Assertions.assertTrue("all counters terminated", counters.stream().allMatch(ActorRef::isTerminated));
        });
    }
}
