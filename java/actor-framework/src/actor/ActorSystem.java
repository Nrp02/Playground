package actor;

import java.time.Duration;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public final class ActorSystem {
    private final String name;
    private final int throughput;
    private final ThreadPoolExecutor dispatcher;
    private final ScheduledThreadPoolExecutor timer;
    private final Scheduler scheduler;
    private final EventStream eventStream = new EventStream();
    private final DeadLetterRef deadLetters;
    private final Map<String, ActorCell> registry = new ConcurrentHashMap<>();
    private final AtomicLong tempCounter = new AtomicLong();
    private final AtomicLong deadLetterCount = new AtomicLong();
    private final AtomicBoolean terminating = new AtomicBoolean();
    private final CompletableFuture<Void> terminated = new CompletableFuture<>();
    private final ActorCell guardian;

    private ActorSystem(String name, int threads, int throughput) {
        this.name = name;
        this.throughput = throughput;
        AtomicInteger threadIds = new AtomicInteger();
        this.dispatcher = new ThreadPoolExecutor(threads, threads, 0L, TimeUnit.MILLISECONDS,
            new LinkedBlockingQueue<>(), runnable -> {
                Thread thread = new Thread(runnable, name + "-dispatcher-" + threadIds.incrementAndGet());
                thread.setDaemon(true);
                return thread;
            });
        this.timer = new ScheduledThreadPoolExecutor(1, runnable -> {
            Thread thread = new Thread(runnable, name + "-scheduler");
            thread.setDaemon(true);
            return thread;
        });
        this.timer.setRemoveOnCancelPolicy(true);
        this.scheduler = new Scheduler(timer);
        this.deadLetters = new DeadLetterRef(this);
        this.guardian = new ActorCell(this, null, "user", Props.create(Guardian::new));
        registry.put(guardian.path(), guardian);
        guardian.terminationFuture().whenComplete((ignored, error) -> releaseThreads(false));
        guardian.sendSystem(new SystemMessage.Create());
    }

    public static ActorSystem create(String name) {
        return create(name, Math.max(2, Runtime.getRuntime().availableProcessors()), 16);
    }

    public static ActorSystem create(String name, int threads, int throughput) {
        Objects.requireNonNull(name, "name");
        if (threads < 1 || throughput < 1) {
            throw new IllegalArgumentException("threads and throughput must be positive");
        }
        return new ActorSystem(name, threads, throughput);
    }

    public String name() {
        return name;
    }

    public ActorRef spawn(Props props, String actorName) {
        return guardian.spawnChild(props, actorName, false);
    }

    public ActorRef spawn(Props props) {
        return guardian.spawn(props);
    }

    public void stop(ActorRef ref) {
        if (ref instanceof LocalActorRef local) {
            local.cell().sendSystem(new SystemMessage.Stop());
        }
    }

    public Optional<ActorRef> lookup(String path) {
        ActorCell cell = registry.get(path);
        return cell == null ? Optional.empty() : Optional.of(cell.self());
    }

    public int liveActorCount() {
        return registry.size() - (guardian.state() == ActorCell.State.TERMINATED ? 0 : 1);
    }

    public ActorRef deadLetters() {
        return deadLetters;
    }

    public long deadLetterCount() {
        return deadLetterCount.get();
    }

    public EventStream eventStream() {
        return eventStream;
    }

    public Scheduler scheduler() {
        return scheduler;
    }

    public long droppedMessages(ActorRef ref) {
        return ref instanceof LocalActorRef local ? local.cell().droppedCount() : 0;
    }

    public int mailboxSize(ActorRef ref) {
        return ref instanceof LocalActorRef local ? local.cell().mailboxSize() : 0;
    }

    public CompletableFuture<Object> ask(ActorRef target, Object message, Duration timeout) {
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(message, "message");
        PromiseRef promise = new PromiseRef(this, "/temp/$" + tempCounter.incrementAndGet());
        CompletableFuture<Object> future = promise.future();
        Cancellable timeoutTask;
        try {
            timeoutTask = scheduler.runOnce(timeout,
                () -> future.completeExceptionally(new AskTimeoutException(target.path(), timeout)));
        } catch (RejectedExecutionException e) {
            future.completeExceptionally(new IllegalStateException("actor system " + name + " is terminated"));
            return future;
        }
        future.whenComplete((value, error) -> timeoutTask.cancel());
        try {
            target.tell(message, promise);
        } catch (RuntimeException e) {
            future.completeExceptionally(e);
        }
        return future;
    }

    public CompletableFuture<Void> terminate() {
        if (terminating.compareAndSet(false, true)) {
            guardian.sendSystem(new SystemMessage.Stop());
        }
        return terminated.copy();
    }

    public boolean shutdown(Duration timeout) throws InterruptedException {
        long deadline = System.nanoTime() + timeout.toNanos();
        terminate();
        try {
            terminated.get(timeout.toNanos(), TimeUnit.NANOSECONDS);
            return dispatcher.awaitTermination(Math.max(0, deadline - System.nanoTime()), TimeUnit.NANOSECONDS);
        } catch (TimeoutException | ExecutionException e) {
            releaseThreads(true);
            return false;
        }
    }

    public CompletableFuture<Void> whenTerminated() {
        return terminated.copy();
    }

    public boolean isTerminated() {
        return terminated.isDone();
    }

    int throughput() {
        return throughput;
    }

    void dispatch(Runnable task) {
        dispatcher.execute(task);
    }

    void register(ActorCell cell) {
        registry.put(cell.path(), cell);
    }

    void unregister(ActorCell cell) {
        registry.remove(cell.path(), cell);
    }

    void publishDeadLetter(Object message, ActorRef sender, ActorRef recipient) {
        deadLetterCount.incrementAndGet();
        eventStream.publish(new Messages.DeadLetter(message, sender, recipient));
    }

    private void releaseThreads(boolean force) {
        timer.shutdownNow();
        if (force) {
            dispatcher.shutdownNow();
        } else {
            dispatcher.shutdown();
        }
        terminated.complete(null);
    }

    private static final class Guardian extends Actor {
        @Override
        public void receive(Object message) {
            unhandled(message);
        }
    }
}
