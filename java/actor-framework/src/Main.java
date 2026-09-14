import actor.Actor;
import actor.ActorRef;
import actor.ActorSystem;
import actor.Directive;
import actor.Messages;
import actor.OverflowStrategy;
import actor.Props;
import actor.Receive;
import actor.Routers;
import actor.Signals;
import actor.SupervisorStrategy;
import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class Main {

    private Main() {
    }

    record Deposit(String account, long amount) {
    }

    record Withdraw(String account, long amount) {
    }

    record Balance(String account) {
    }

    static final class Bank extends Actor {
        private final Map<String, Long> balances = new HashMap<>();

        @Override
        public void preStart() {
            System.out.println("  bank started at " + self().path());
        }

        @Override
        public void postRestart(Throwable reason) {
            System.out.println("  bank restarted after: " + reason.getMessage() + " (balances reset)");
        }

        @Override
        public void receive(Object message) {
            switch (message) {
                case Deposit deposit -> balances.merge(deposit.account(), deposit.amount(), Long::sum);
                case Withdraw withdraw -> {
                    long current = balances.getOrDefault(withdraw.account(), 0L);
                    if (current < withdraw.amount()) {
                        throw new IllegalStateException("overdraft on " + withdraw.account());
                    }
                    balances.put(withdraw.account(), current - withdraw.amount());
                }
                case Balance balance -> sender().tell(balances.getOrDefault(balance.account(), 0L), self());
                default -> unhandled(message);
            }
        }
    }

    static final class TrafficLight extends Actor {
        private final Receive green = message -> {
            if ("next".equals(message)) {
                context().become(yellow());
            }
            sender().tell("green", self());
        };

        private Receive yellow() {
            return message -> {
                if ("next".equals(message)) {
                    context().unbecome();
                }
                sender().tell("yellow", self());
            };
        }

        @Override
        public void receive(Object message) {
            if ("next".equals(message)) {
                context().become(green, false);
            }
            sender().tell("red", self());
        }
    }

    static final class Worker extends Actor {
        @Override
        public void receive(Object message) throws InterruptedException {
            if (message instanceof Integer n) {
                Thread.sleep(2);
                sender().tell(self().path().substring(self().path().lastIndexOf('/') + 1) + " squared " + n
                    + " = " + (n * n), self());
            }
        }
    }

    static final class SlowSink extends Actor {
        private final CountDownLatch release;

        SlowSink(CountDownLatch release) {
            this.release = release;
        }

        @Override
        public void receive(Object message) throws InterruptedException {
            if ("hold".equals(message)) {
                release.await(2, TimeUnit.SECONDS);
            }
        }
    }

    static final class Watcher extends Actor {
        private final ActorRef target;
        private final CountDownLatch seen;

        Watcher(ActorRef target, CountDownLatch seen) {
            this.target = target;
            this.seen = seen;
        }

        @Override
        public void preStart() {
            context().watch(target);
        }

        @Override
        public void receive(Object message) {
            if (message instanceof Messages.Terminated terminated) {
                System.out.println("  watcher saw termination of " + terminated.actor().path());
                seen.countDown();
            }
        }
    }

    public static void main(String[] args) throws Exception {
        ActorSystem system = ActorSystem.create("demo", 4, 16);
        system.eventStream().subscribe(Messages.ErrorEvent.class,
            event -> System.out.println("  [error] " + event.actor().path() + ": " + event.cause().getMessage()));
        Duration timeout = Duration.ofSeconds(2);

        System.out.println("== tell / ask and supervision");
        ActorRef bank = system.spawn(Props.create(Bank::new)
            .withSupervisorStrategy(SupervisorStrategy.oneForOne(3, Duration.ofSeconds(10), cause -> Directive.RESTART)), "bank");
        bank.tell(new Deposit("alice", 100));
        bank.tell(new Deposit("bob", 40));
        bank.tell(new Withdraw("alice", 30));
        System.out.println("  alice = " + bank.ask(new Balance("alice"), timeout).get());
        bank.tell(new Withdraw("bob", 500));
        System.out.println("  bob after overdraft + restart = " + bank.ask(new Balance("bob"), timeout).get());

        System.out.println("== become / unbecome");
        ActorRef light = system.spawn(Props.create(TrafficLight::new), "light");
        StringBuilder sequence = new StringBuilder();
        for (int i = 0; i < 6; i++) {
            sequence.append(light.ask("next", timeout).get()).append(i < 5 ? " -> " : "");
        }
        System.out.println("  " + sequence);

        System.out.println("== round-robin router");
        ActorRef pool = system.spawn(Routers.roundRobinPool(3, Props.create(Worker::new)), "workers");
        for (int n = 1; n <= 6; n++) {
            System.out.println("  " + pool.ask(n, timeout).get());
        }

        System.out.println("== bounded mailbox backpressure");
        CountDownLatch release = new CountDownLatch(1);
        ActorRef sink = system.spawn(Props.create(() -> new SlowSink(release))
            .withMailbox(5, OverflowStrategy.DROP_OLDEST), "sink");
        sink.tell("hold");
        Thread.sleep(20);
        for (int i = 0; i < 20; i++) {
            sink.tell("event-" + i);
        }
        System.out.println("  queued=" + system.mailboxSize(sink) + " dropped=" + system.droppedMessages(sink)
            + " deadLetters=" + system.deadLetterCount());
        release.countDown();

        System.out.println("== death watch and scheduler");
        CountDownLatch seen = new CountDownLatch(1);
        ActorRef doomed = system.spawn(Props.create(Worker::new), "doomed");
        system.spawn(Props.create(() -> new Watcher(doomed, seen)), "watcher");
        system.scheduler().scheduleOnce(Duration.ofMillis(50), doomed, Signals.POISON_PILL);
        seen.await(2, TimeUnit.SECONDS);

        System.out.println("== graceful shutdown");
        System.out.println("  live actors before shutdown: " + system.liveActorCount());
        boolean graceful = system.shutdown(Duration.ofSeconds(5));
        System.out.println("  graceful=" + graceful + " terminated=" + system.isTerminated()
            + " live actors after: " + system.liveActorCount());
    }
}
