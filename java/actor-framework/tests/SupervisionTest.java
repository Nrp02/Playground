import actor.Actor;
import actor.ActorRef;
import actor.Directive;
import actor.Messages;
import actor.Props;
import actor.Signals;
import actor.SupervisorStrategy;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;

public final class SupervisionTest {

    private SupervisionTest() {
    }

    static final class Supervisor extends Actor {
        private final SupervisorStrategy strategy;

        Supervisor(SupervisorStrategy strategy) {
            this.strategy = strategy;
        }

        SupervisorStrategy strategy() {
            return strategy;
        }

        @Override
        public void receive(Object message) {
            if (message instanceof ChildSpec spec) {
                sender().tell(context().spawn(spec.props(), spec.name()), self());
            }
        }
    }

    record ChildSpec(String name, Props props) {
    }

    static ActorRef supervisor(actor.ActorSystem system, String name, SupervisorStrategy strategy) {
        return system.spawn(Props.create(() -> new Supervisor(strategy)).withSupervisorStrategy(strategy), name);
    }

    static ActorRef child(ActorRef supervisor, String name, Props props) throws Exception {
        return Assertions.await(supervisor.ask(new ChildSpec(name, props), Duration.ofSeconds(1), ActorRef.class));
    }

    static int count(ActorRef counter) throws Exception {
        return Assertions.await(counter.ask("get", Duration.ofSeconds(1), Integer.class));
    }

    static Function<Throwable, Directive> always(Directive directive) {
        return cause -> directive;
    }

    public static void run() {
        Assertions.suite("supervision");

        Assertions.test("restart replaces the instance and resets state", system -> {
            ActorRef sup = supervisor(system, "sup", SupervisorStrategy.defaultStrategy());
            ActorRef counter = child(sup, "counter", Props.create(TestActors.Counter::new));
            counter.tell("inc");
            counter.tell("inc");
            Assertions.assertEquals("before failure", 2, count(counter));
            counter.tell("boom");
            Assertions.assertEquals("after restart", 0, count(counter));
            Assertions.assertFalse("same ref still alive", counter.isTerminated());
        });

        Assertions.test("restart hooks see the cause and failing message", system -> {
            List<String> log = new CopyOnWriteArrayList<>();
            ActorRef sup = supervisor(system, "sup", SupervisorStrategy.defaultStrategy());
            ActorRef recorder = child(sup, "rec", Props.create(() -> new TestActors.Recorder("r", log)));
            recorder.tell("fail:oops");
            Assertions.await(recorder.ask("after", Duration.ofSeconds(1)));
            Assertions.assertEquals("hooks", List.of("r:preStart", "r:preRestart:oops:fail:oops", "r:postRestart:oops",
                "r:after"), List.copyOf(log));
        });

        Assertions.test("resume keeps actor state", system -> {
            SupervisorStrategy resume = SupervisorStrategy.oneForOne(-1, Duration.ofMinutes(1), always(Directive.RESUME));
            ActorRef sup = supervisor(system, "sup", resume);
            ActorRef counter = child(sup, "counter", Props.create(TestActors.Counter::new));
            counter.tell("inc");
            counter.tell("boom");
            counter.tell("inc");
            Assertions.assertEquals("state survived", 2, count(counter));
        });

        Assertions.test("stop directive terminates the child", system -> {
            ActorRef sup = supervisor(system, "sup", SupervisorStrategy.stoppingStrategy());
            ActorRef counter = child(sup, "counter", Props.create(TestActors.Counter::new));
            counter.tell("boom");
            Assertions.await(counter.whenTerminated());
            Assertions.assertFalse("supervisor unaffected", sup.isTerminated());
        });

        Assertions.test("exceeding max retries stops the child", system -> {
            SupervisorStrategy limited = SupervisorStrategy.oneForOne(2, Duration.ofMinutes(1), always(Directive.RESTART));
            ActorRef sup = supervisor(system, "sup", limited);
            ActorRef counter = child(sup, "counter", Props.create(TestActors.Counter::new));
            counter.tell("boom");
            Assertions.assertEquals("restart 1", 0, count(counter));
            counter.tell("boom");
            Assertions.assertEquals("restart 2", 0, count(counter));
            counter.tell("boom");
            Assertions.await(counter.whenTerminated());
        });

        Assertions.test("all-for-one restarts every sibling", system -> {
            SupervisorStrategy all = SupervisorStrategy.allForOne(5, Duration.ofMinutes(1), always(Directive.RESTART));
            ActorRef sup = supervisor(system, "sup", all);
            ActorRef left = child(sup, "left", Props.create(TestActors.Counter::new));
            ActorRef right = child(sup, "right", Props.create(TestActors.Counter::new));
            left.tell("inc");
            right.tell("inc");
            right.tell("inc");
            Assertions.assertEquals("right before", 2, count(right));
            left.tell("boom");
            Assertions.assertEquals("left reset", 0, count(left));
            Assertions.eventually("right reset too", () -> {
                try {
                    return count(right) == 0;
                } catch (Exception e) {
                    return false;
                }
            });
        });

        Assertions.test("one-for-one leaves siblings alone", system -> {
            ActorRef sup = supervisor(system, "sup", SupervisorStrategy.defaultStrategy());
            ActorRef left = child(sup, "left", Props.create(TestActors.Counter::new));
            ActorRef right = child(sup, "right", Props.create(TestActors.Counter::new));
            right.tell("inc");
            left.tell("boom");
            Assertions.assertEquals("left reset", 0, count(left));
            Assertions.assertEquals("right untouched", 1, count(right));
        });

        Assertions.test("escalation restarts the parent and its children", system -> {
            AtomicInteger middleInstances = new AtomicInteger();
            SupervisorStrategy escalate = SupervisorStrategy.oneForOne(-1, Duration.ofMinutes(1), always(Directive.ESCALATE));
            SupervisorStrategy restart = SupervisorStrategy.oneForOne(-1, Duration.ofMinutes(1), always(Directive.RESTART));
            ActorRef top = supervisor(system, "top", restart);
            ActorRef middle = child(top, "middle", Props.create(() -> {
                middleInstances.incrementAndGet();
                return new Supervisor(escalate);
            }).withSupervisorStrategy(escalate));
            ActorRef leaf = child(middle, "leaf", Props.create(TestActors.Counter::new));
            leaf.tell("inc");
            Assertions.assertEquals("leaf counted", 1, count(leaf));
            leaf.tell("boom");
            Assertions.await(leaf.whenTerminated());
            Assertions.eventually("middle restarted", () -> middleInstances.get() == 2);
            Assertions.assertFalse("middle ref still valid", middle.isTerminated());
            ActorRef newLeaf = child(middle, "leaf", Props.create(TestActors.Counter::new));
            Assertions.assertEquals("fresh leaf", 0, count(newLeaf));
        });

        Assertions.test("Kill stops the actor under the default decider", system -> {
            ActorRef sup = supervisor(system, "sup", SupervisorStrategy.defaultStrategy());
            ActorRef counter = child(sup, "counter", Props.create(TestActors.Counter::new));
            counter.tell(Signals.KILL);
            Assertions.await(counter.whenTerminated());
        });

        Assertions.test("errors escalate to the guardian which stops the actor", system -> {
            ActorRef counter = system.spawn(Props.create(TestActors.Counter::new), "counter");
            counter.tell("fatal");
            Assertions.await(counter.whenTerminated());
            Assertions.assertFalse("system still running", system.isTerminated());
        });

        Assertions.test("failures are published as error events", system -> {
            List<Messages.ErrorEvent> errors = new CopyOnWriteArrayList<>();
            system.eventStream().subscribe(Messages.ErrorEvent.class, errors::add);
            ActorRef counter = system.spawn(Props.create(TestActors.Counter::new), "counter");
            counter.tell("boom");
            Assertions.eventually("event", () -> errors.size() == 1);
            Assertions.assertEquals("actor", counter, errors.get(0).actor());
            Assertions.assertEquals("message", "boom", errors.get(0).message());
        });

        Assertions.test("preStart failures are retried by restart", system -> {
            AtomicInteger attempts = new AtomicInteger();
            ActorRef flaky = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void preStart() {
                    if (attempts.incrementAndGet() < 3) {
                        throw new IllegalStateException("not yet");
                    }
                }

                @Override
                public void receive(Object message) {
                    sender().tell("ready after " + attempts.get(), self());
                }
            }), "flaky");
            Assertions.assertEquals("third attempt succeeded", "ready after 3",
                Assertions.await(flaky.ask("status", Duration.ofSeconds(1))));
        });

        Assertions.test("messages queued during a restart are not lost", system -> {
            ActorRef counter = system.spawn(Props.create(TestActors.Counter::new), "counter");
            counter.tell("boom");
            for (int i = 0; i < 50; i++) {
                counter.tell("inc");
            }
            Assertions.assertEquals("all increments survive", 50, count(counter));
        });
    }
}
