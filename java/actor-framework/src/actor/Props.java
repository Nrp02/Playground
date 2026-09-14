package actor;

import java.time.Duration;
import java.util.Objects;
import java.util.function.Supplier;

public final class Props {
    private final Supplier<? extends Actor> factory;
    private final int mailboxCapacity;
    private final OverflowStrategy overflowStrategy;
    private final Duration blockTimeout;
    private final SupervisorStrategy supervisorStrategy;

    private Props(Supplier<? extends Actor> factory, int mailboxCapacity, OverflowStrategy overflowStrategy,
                  Duration blockTimeout, SupervisorStrategy supervisorStrategy) {
        this.factory = factory;
        this.mailboxCapacity = mailboxCapacity;
        this.overflowStrategy = overflowStrategy;
        this.blockTimeout = blockTimeout;
        this.supervisorStrategy = supervisorStrategy;
    }

    public static Props create(Supplier<? extends Actor> factory) {
        return new Props(Objects.requireNonNull(factory, "factory"), Integer.MAX_VALUE, OverflowStrategy.DROP_NEW,
            Duration.ofSeconds(1), SupervisorStrategy.defaultStrategy());
    }

    public Props withMailbox(int capacity, OverflowStrategy strategy) {
        if (capacity < 1) {
            throw new IllegalArgumentException("mailbox capacity must be positive");
        }
        return new Props(factory, capacity, Objects.requireNonNull(strategy, "strategy"), blockTimeout, supervisorStrategy);
    }

    public Props withBlockTimeout(Duration timeout) {
        if (timeout.isNegative()) {
            throw new IllegalArgumentException("block timeout must not be negative");
        }
        return new Props(factory, mailboxCapacity, overflowStrategy, timeout, supervisorStrategy);
    }

    public Props withSupervisorStrategy(SupervisorStrategy strategy) {
        return new Props(factory, mailboxCapacity, overflowStrategy, blockTimeout, Objects.requireNonNull(strategy, "strategy"));
    }

    public int mailboxCapacity() {
        return mailboxCapacity;
    }

    public OverflowStrategy overflowStrategy() {
        return overflowStrategy;
    }

    public SupervisorStrategy supervisorStrategy() {
        return supervisorStrategy;
    }

    Actor newActor() {
        Actor actor = factory.get();
        if (actor == null) {
            throw new IllegalStateException("actor factory returned null");
        }
        return actor;
    }

    Mailbox newMailbox() {
        return new Mailbox(mailboxCapacity, overflowStrategy, blockTimeout);
    }
}
