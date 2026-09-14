package actor;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public final class Routers {

    public enum Logic {
        ROUND_ROBIN,
        BROADCAST,
        SMALLEST_MAILBOX
    }

    private Routers() {
    }

    public static Props roundRobinPool(int size, Props routee) {
        return pool(Logic.ROUND_ROBIN, size, routee);
    }

    public static Props broadcastPool(int size, Props routee) {
        return pool(Logic.BROADCAST, size, routee);
    }

    public static Props smallestMailboxPool(int size, Props routee) {
        return pool(Logic.SMALLEST_MAILBOX, size, routee);
    }

    public static Props pool(Logic logic, int size, Props routee) {
        Objects.requireNonNull(logic, "logic");
        Objects.requireNonNull(routee, "routee");
        if (size < 1) {
            throw new IllegalArgumentException("router pool size must be positive");
        }
        return Props.create(() -> new RouterActor(logic, size, routee));
    }

    private static final class RouterActor extends Actor {
        private final Logic logic;
        private final int size;
        private final Props routeeProps;
        private final List<ActorRef> routees = new ArrayList<>();
        private int next;

        RouterActor(Logic logic, int size, Props routeeProps) {
            this.logic = logic;
            this.size = size;
            this.routeeProps = routeeProps;
        }

        @Override
        public void preStart() {
            routees.clear();
            for (int i = 0; i < size; i++) {
                routees.add(context().watch(context().spawn(routeeProps, "routee-" + i)));
            }
        }

        @Override
        public void receive(Object message) {
            if (message instanceof Messages.Terminated terminated) {
                routees.remove(terminated.actor());
                if (routees.isEmpty()) {
                    context().stop(self());
                }
                return;
            }
            if (message == Signals.GET_ROUTEES) {
                sender().tell(List.copyOf(routees), self());
                return;
            }
            if (message instanceof Messages.Broadcast broadcast) {
                sendToAll(broadcast.message());
                return;
            }
            switch (logic) {
                case ROUND_ROBIN -> {
                    ActorRef target = routees.get(next % routees.size());
                    next = (next + 1) % routees.size();
                    target.tell(message, sender());
                }
                case BROADCAST -> sendToAll(message);
                case SMALLEST_MAILBOX -> smallest().tell(message, sender());
            }
        }

        private void sendToAll(Object message) {
            for (ActorRef routee : routees) {
                routee.tell(message, sender());
            }
        }

        private ActorRef smallest() {
            ActorRef best = routees.get(0);
            int bestSize = Integer.MAX_VALUE;
            for (ActorRef routee : routees) {
                int queued = routee instanceof LocalActorRef local ? local.cell().load() : Integer.MAX_VALUE;
                if (queued < bestSize) {
                    best = routee;
                    bestSize = queued;
                }
            }
            return best;
        }
    }
}
