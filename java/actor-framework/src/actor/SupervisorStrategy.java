package actor;

import java.time.Duration;
import java.util.Objects;
import java.util.function.Function;

public final class SupervisorStrategy {

    public enum Scope {
        ONE_FOR_ONE,
        ALL_FOR_ONE
    }

    private final Scope scope;
    private final int maxRetries;
    private final Duration window;
    private final Function<Throwable, Directive> decider;

    private SupervisorStrategy(Scope scope, int maxRetries, Duration window, Function<Throwable, Directive> decider) {
        if (maxRetries < -1) {
            throw new IllegalArgumentException("maxRetries must be -1 (unlimited) or non-negative");
        }
        this.scope = Objects.requireNonNull(scope, "scope");
        this.maxRetries = maxRetries;
        this.window = Objects.requireNonNull(window, "window");
        this.decider = Objects.requireNonNull(decider, "decider");
    }

    public static SupervisorStrategy oneForOne(int maxRetries, Duration window, Function<Throwable, Directive> decider) {
        return new SupervisorStrategy(Scope.ONE_FOR_ONE, maxRetries, window, decider);
    }

    public static SupervisorStrategy allForOne(int maxRetries, Duration window, Function<Throwable, Directive> decider) {
        return new SupervisorStrategy(Scope.ALL_FOR_ONE, maxRetries, window, decider);
    }

    public static SupervisorStrategy defaultStrategy() {
        return oneForOne(10, Duration.ofMinutes(1), defaultDecider());
    }

    public static SupervisorStrategy stoppingStrategy() {
        return oneForOne(0, Duration.ofMinutes(1), cause -> Directive.STOP);
    }

    public static Function<Throwable, Directive> defaultDecider() {
        return cause -> {
            if (cause instanceof ActorKilledException) {
                return Directive.STOP;
            }
            if (cause instanceof Exception) {
                return Directive.RESTART;
            }
            return Directive.ESCALATE;
        };
    }

    public Scope scope() {
        return scope;
    }

    public int maxRetries() {
        return maxRetries;
    }

    public Duration window() {
        return window;
    }

    Directive decide(Throwable cause) {
        try {
            Directive directive = decider.apply(cause);
            return directive == null ? Directive.ESCALATE : directive;
        } catch (RuntimeException e) {
            return Directive.ESCALATE;
        }
    }

    boolean permitRestart(RestartStats stats, long nowNanos) {
        if (maxRetries < 0) {
            return true;
        }
        if (stats.count == 0 || nowNanos - stats.windowStartNanos > window.toNanos()) {
            stats.count = 0;
            stats.windowStartNanos = nowNanos;
        }
        stats.count++;
        return stats.count <= maxRetries;
    }

    static final class RestartStats {
        int count;
        long windowStartNanos;
    }
}
