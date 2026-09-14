package actor;

import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

public final class Scheduler {
    private final ScheduledExecutorService timer;

    Scheduler(ScheduledExecutorService timer) {
        this.timer = timer;
    }

    public Cancellable scheduleOnce(Duration delay, ActorRef target, Object message) {
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(message, "message");
        return runOnce(delay, () -> target.tell(message));
    }

    public Cancellable scheduleAtFixedRate(Duration initialDelay, Duration interval, ActorRef target, Object message) {
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(message, "message");
        if (interval.isZero() || interval.isNegative()) {
            throw new IllegalArgumentException("interval must be positive");
        }
        ScheduledFuture<?> future = timer.scheduleAtFixedRate(() -> target.tell(message),
            initialDelay.toNanos(), interval.toNanos(), TimeUnit.NANOSECONDS);
        Cancellable cancellable = new FutureCancellable(future);
        target.whenTerminated().thenRun(cancellable::cancel);
        return cancellable;
    }

    public Cancellable runOnce(Duration delay, Runnable task) {
        Objects.requireNonNull(task, "task");
        return new FutureCancellable(timer.schedule(task, delay.toNanos(), TimeUnit.NANOSECONDS));
    }

    private record FutureCancellable(ScheduledFuture<?> future) implements Cancellable {
        @Override
        public boolean cancel() {
            return future.cancel(false);
        }

        @Override
        public boolean isCancelled() {
            return future.isCancelled();
        }
    }
}
