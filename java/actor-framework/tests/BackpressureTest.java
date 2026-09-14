import actor.ActorRef;
import actor.MailboxOverflowException;
import actor.OverflowStrategy;
import actor.Props;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class BackpressureTest {

    private BackpressureTest() {
    }

    record Gated(ActorRef ref, CountDownLatch release, List<Object> seen) {
    }

    static Gated gated(actor.ActorSystem system, int capacity, OverflowStrategy strategy, Duration blockTimeout)
        throws InterruptedException {
        CountDownLatch started = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        List<Object> seen = new CopyOnWriteArrayList<>();
        ActorRef ref = system.spawn(Props.create(() -> new TestActors.Gate(started, release, seen))
            .withMailbox(capacity, strategy).withBlockTimeout(blockTimeout), "gate");
        ref.tell("hold");
        Assertions.assertTrue("gate started", started.await(3, TimeUnit.SECONDS));
        return new Gated(ref, release, seen);
    }

    public static void run() {
        Assertions.suite("backpressure");

        Assertions.test("DROP_NEW keeps the oldest messages", system -> {
            Gated gate = gated(system, 3, OverflowStrategy.DROP_NEW, Duration.ZERO);
            for (int i = 1; i <= 6; i++) {
                gate.ref().tell(i);
            }
            Assertions.assertEquals("dropped", 3L, system.droppedMessages(gate.ref()));
            Assertions.assertEquals("dead letters", 3L, system.deadLetterCount());
            gate.release().countDown();
            CompletableFuture<Object> done = gate.ref().ask("done", Duration.ofSeconds(2));
            Assertions.await(done);
            Assertions.assertEquals("survivors", List.of(1, 2, 3, "done"), List.copyOf(gate.seen()));
        });

        Assertions.test("DROP_OLDEST keeps the newest messages", system -> {
            Gated gate = gated(system, 3, OverflowStrategy.DROP_OLDEST, Duration.ZERO);
            for (int i = 1; i <= 6; i++) {
                gate.ref().tell(i);
            }
            Assertions.assertEquals("dropped", 3L, system.droppedMessages(gate.ref()));
            gate.release().countDown();
            Assertions.eventually("drained", () -> gate.seen().size() == 3);
            Assertions.assertEquals("survivors", List.of(4, 5, 6), List.copyOf(gate.seen()));
        });

        Assertions.test("REJECT throws to the sender", system -> {
            Gated gate = gated(system, 2, OverflowStrategy.REJECT, Duration.ZERO);
            gate.ref().tell("a");
            gate.ref().tell("b");
            Assertions.assertThrows("overflow", MailboxOverflowException.class, () -> gate.ref().tell("c"));
            Assertions.assertEquals("queued", 2, system.mailboxSize(gate.ref()));
            gate.release().countDown();
            Assertions.eventually("drained", () -> gate.seen().size() == 2);
        });

        Assertions.test("BLOCK waits for space and then enqueues", system -> {
            Gated gate = gated(system, 1, OverflowStrategy.BLOCK, Duration.ofSeconds(2));
            gate.ref().tell("first");
            CompletableFuture<Void> blocked = CompletableFuture.runAsync(() -> gate.ref().tell("second"));
            Thread.sleep(50);
            Assertions.assertFalse("sender is blocked", blocked.isDone());
            gate.release().countDown();
            Assertions.await(blocked);
            Assertions.eventually("both processed", () -> gate.seen().size() == 2);
            Assertions.assertEquals("order", List.of("first", "second"), List.copyOf(gate.seen()));
        });

        Assertions.test("BLOCK gives up after its timeout", system -> {
            Gated gate = gated(system, 1, OverflowStrategy.BLOCK, Duration.ofMillis(40));
            gate.ref().tell("first");
            long start = System.nanoTime();
            Assertions.assertThrows("timed out", MailboxOverflowException.class, () -> gate.ref().tell("second"));
            Assertions.assertTrue("waited roughly the timeout",
                System.nanoTime() - start >= TimeUnit.MILLISECONDS.toNanos(35));
            gate.release().countDown();
        });

        Assertions.test("blocked senders are released when the actor stops", system -> {
            Gated gate = gated(system, 1, OverflowStrategy.BLOCK, Duration.ofSeconds(5));
            gate.ref().tell("first");
            CompletableFuture<Void> blocked = CompletableFuture.runAsync(() -> gate.ref().tell("second"));
            Thread.sleep(30);
            system.stop(gate.ref());
            gate.release().countDown();
            Assertions.await(gate.ref().whenTerminated());
            Assertions.await(blocked);
            Assertions.eventually("unsent message dead-lettered", () -> system.deadLetterCount() >= 1);
        });
    }
}
