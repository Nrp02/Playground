import actor.Actor;
import actor.ActorRef;
import actor.Messages;
import actor.Props;
import actor.Receive;
import java.time.Duration;

public final class BehaviorTest {

    private BehaviorTest() {
    }

    static final class Door extends Actor {
        private final Receive open = message -> {
            switch (String.valueOf(message)) {
                case "close" -> context().unbecome();
                case "state" -> sender().tell("open", self());
                case "peek" -> context().become(m -> {
                    if ("back".equals(m)) {
                        context().unbecome();
                    } else {
                        sender().tell("peeking", self());
                    }
                }, false);
                default -> unhandled(message);
            }
        };

        @Override
        public void receive(Object message) {
            switch (String.valueOf(message)) {
                case "open" -> context().become(open);
                case "state" -> sender().tell("closed", self());
                case "crash" -> throw new IllegalStateException("crash");
                default -> unhandled(message);
            }
        }
    }

    static String state(ActorRef door) throws Exception {
        return Assertions.await(door.ask("state", Duration.ofSeconds(1), String.class));
    }

    public static void run() {
        Assertions.suite("behaviors and watch");

        Assertions.test("become and unbecome switch behaviors", system -> {
            ActorRef door = system.spawn(Props.create(Door::new), "door");
            Assertions.assertEquals("initial", "closed", state(door));
            door.tell("open");
            Assertions.assertEquals("opened", "open", state(door));
            door.tell("close");
            Assertions.assertEquals("closed again", "closed", state(door));
        });

        Assertions.test("stacked behaviors unwind one level at a time", system -> {
            ActorRef door = system.spawn(Props.create(Door::new), "door");
            door.tell("open");
            door.tell("peek");
            Assertions.assertEquals("top of stack", "peeking", state(door));
            door.tell("back");
            Assertions.assertEquals("previous behavior", "open", state(door));
        });

        Assertions.test("restart resets the behavior stack", system -> {
            ActorRef door = system.spawn(Props.create(Door::new), "door");
            door.tell("open");
            Assertions.assertEquals("opened", "open", state(door));
            door.tell("close");
            door.tell("crash");
            Assertions.assertEquals("initial behavior after restart", "closed", state(door));
        });

        Assertions.test("watchers receive Terminated", system -> {
            TestProbe probe = new TestProbe(system);
            ActorRef target = system.spawn(Props.create(TestActors.Echo::new), "target");
            ActorRef watcher = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void preStart() {
                    context().watch(target);
                    probe.ref().tell("watching", self());
                }

                @Override
                public void receive(Object message) {
                    probe.ref().tell(message, self());
                }
            }), "watcher");
            Assertions.assertEquals("ready", "watching", probe.expectMsg());
            system.stop(target);
            Messages.Terminated terminated = probe.expectMsgClass(Messages.Terminated.class);
            Assertions.assertEquals("terminated actor", target, terminated.actor());
            Assertions.assertFalse("watcher lives on", watcher.isTerminated());
        });

        Assertions.test("watching a dead actor delivers Terminated immediately", system -> {
            TestProbe probe = new TestProbe(system);
            ActorRef target = system.spawn(Props.create(TestActors.Echo::new), "target");
            system.stop(target);
            Assertions.await(target.whenTerminated());
            system.spawn(Props.create(() -> new Actor() {
                @Override
                public void preStart() {
                    context().watch(target);
                }

                @Override
                public void receive(Object message) {
                    probe.ref().tell(message, self());
                }
            }), "late-watcher");
            Assertions.assertEquals("terminated", target, probe.expectMsgClass(Messages.Terminated.class).actor());
        });

        Assertions.test("unwatch suppresses Terminated", system -> {
            TestProbe probe = new TestProbe(system);
            ActorRef target = system.spawn(Props.create(TestActors.Echo::new), "target");
            ActorRef watcher = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void preStart() {
                    context().watch(target);
                }

                @Override
                public void receive(Object message) {
                    if ("unwatch".equals(message)) {
                        context().unwatch(target);
                        sender().tell("done", self());
                    } else {
                        probe.ref().tell(message, self());
                    }
                }
            }), "watcher");
            Assertions.await(watcher.ask("unwatch", Duration.ofSeconds(1)));
            system.stop(target);
            Assertions.await(target.whenTerminated());
            probe.expectNoMsg(Duration.ofMillis(100));
        });

        Assertions.test("a parent can only stop itself or its children", system -> {
            ActorRef stranger = system.spawn(Props.create(TestActors.Echo::new), "stranger");
            ActorRef meddler = system.spawn(Props.create(() -> new Actor() {
                @Override
                public void receive(Object message) {
                    try {
                        context().stop(stranger);
                        sender().tell("stopped", self());
                    } catch (IllegalArgumentException e) {
                        sender().tell("refused", self());
                    }
                }
            }), "meddler");
            Assertions.assertEquals("refused", "refused", Assertions.await(meddler.ask("go", Duration.ofSeconds(1))));
            Assertions.assertFalse("stranger alive", stranger.isTerminated());
        });
    }
}
