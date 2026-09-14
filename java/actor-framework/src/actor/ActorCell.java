package actor;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

final class ActorCell implements ActorContext {

    enum State {
        NEW,
        RUNNING,
        TERMINATING,
        TERMINATED
    }

    private enum Pending {
        NONE,
        RESTART,
        TERMINATE
    }

    private final ActorSystem system;
    private final ActorCell parent;
    private final String name;
    private final String path;
    private final Props props;
    private final LocalActorRef self;
    private final Mailbox mailbox;
    private final ConcurrentLinkedQueue<SystemMessage> systemQueue = new ConcurrentLinkedQueue<>();
    private final AtomicBoolean scheduled = new AtomicBoolean();
    private final Map<String, ActorCell> children = new LinkedHashMap<>();
    private final AtomicLong nameCounter = new AtomicLong();
    private final CompletableFuture<Void> termination = new CompletableFuture<>();
    private final Set<ActorRef> watchers = new HashSet<>();
    private final Set<ActorRef> watching = new HashSet<>();
    private final Map<ActorCell, SupervisorStrategy.RestartStats> restartStats = new HashMap<>();
    private final Set<ActorCell> failedChildren = new HashSet<>();
    private final ArrayDeque<Receive> behaviors = new ArrayDeque<>();

    private volatile State state = State.NEW;
    private volatile boolean suspended = true;
    private volatile Pending pending = Pending.NONE;
    private volatile boolean processing;
    private Actor actor;
    private Envelope current;
    private Throwable restartCause;
    private Object failedMessage;

    ActorCell(ActorSystem system, ActorCell parent, String name, Props props) {
        this.system = system;
        this.parent = parent;
        this.name = name;
        this.path = parent == null ? "/" + name : parent.path + "/" + name;
        this.props = props;
        this.mailbox = props.newMailbox();
        this.self = new LocalActorRef(this);
    }

    String path() {
        return path;
    }

    State state() {
        return state;
    }

    int mailboxSize() {
        return mailbox.size();
    }

    int load() {
        return mailbox.size() + (processing ? 1 : 0);
    }

    long droppedCount() {
        return mailbox.droppedCount();
    }

    CompletableFuture<Void> terminationFuture() {
        return termination;
    }

    void sendUser(Envelope envelope) {
        Envelope rejected = mailbox.offer(envelope, path);
        if (rejected != null) {
            system.publishDeadLetter(rejected.message(), rejected.sender(), self);
        }
        if (rejected != envelope) {
            schedule();
        }
    }

    void sendSystem(SystemMessage message) {
        systemQueue.add(message);
        if (state == State.TERMINATED) {
            drainAfterTermination();
        } else {
            schedule();
        }
    }

    private void schedule() {
        if (scheduled.compareAndSet(false, true)) {
            try {
                system.dispatch(this::run);
            } catch (RejectedExecutionException e) {
                scheduled.set(false);
            }
        }
    }

    private void run() {
        try {
            processSystemMessages();
            int budget = system.throughput();
            while (budget > 0 && canProcessUser()) {
                Envelope envelope = mailbox.poll();
                if (envelope == null) {
                    break;
                }
                invoke(envelope);
                budget--;
                processSystemMessages();
            }
        } finally {
            scheduled.set(false);
            if (!systemQueue.isEmpty() || (canProcessUser() && !mailbox.isEmpty())) {
                schedule();
            }
        }
    }

    private boolean canProcessUser() {
        return state == State.RUNNING && !suspended && pending == Pending.NONE;
    }

    private void processSystemMessages() {
        SystemMessage message;
        while ((message = systemQueue.poll()) != null) {
            if (state == State.TERMINATED) {
                handleAfterTermination(message);
            } else {
                handleSystem(message);
            }
        }
    }

    private void handleSystem(SystemMessage message) {
        switch (message) {
            case SystemMessage.Create _ -> create();
            case SystemMessage.Stop _ -> beginTermination();
            case SystemMessage.Resume _ -> resume();
            case SystemMessage.Restart restart -> beginRestart(restart.cause());
            case SystemMessage.Failed failed -> handleChildFailure(failed.child(), failed.cause());
            case SystemMessage.ChildTerminated terminated -> childTerminated(terminated.child());
            case SystemMessage.Watch watch -> addWatcher(watch.watcher());
            case SystemMessage.Unwatch unwatch -> watchers.remove(unwatch.watcher());
        }
    }

    private synchronized void drainAfterTermination() {
        SystemMessage message;
        while ((message = systemQueue.poll()) != null) {
            handleAfterTermination(message);
        }
    }

    private void handleAfterTermination(SystemMessage message) {
        if (message instanceof SystemMessage.Watch watch) {
            watch.watcher().tell(new Messages.Terminated(self), self);
        }
    }

    private void create() {
        synchronized (children) {
            if (state != State.NEW) {
                return;
            }
            state = State.RUNNING;
        }
        try {
            actor = newActorInstance();
            actor.preStart();
            suspended = false;
        } catch (Throwable cause) {
            fail(cause, null);
        }
    }

    private Actor newActorInstance() {
        actor = null;
        behaviors.clear();
        Actor instance = props.newActor();
        instance.bind(this);
        return instance;
    }

    private void invoke(Envelope envelope) {
        current = envelope;
        processing = true;
        Object message = envelope.message();
        try {
            if (message == Signals.POISON_PILL) {
                beginTermination();
            } else if (message == Signals.KILL) {
                throw new ActorKilledException(path);
            } else if (message instanceof Messages.Terminated terminated && terminated.actor() == envelope.sender()) {
                if (watching.remove(terminated.actor())) {
                    deliver(message);
                }
            } else {
                deliver(message);
            }
        } catch (Throwable cause) {
            fail(cause, message);
        } finally {
            current = null;
            processing = false;
        }
    }

    private void deliver(Object message) throws Exception {
        Receive behavior = behaviors.peek();
        if (behavior != null) {
            behavior.onMessage(message);
        } else {
            actor.receive(message);
        }
    }

    private void fail(Throwable cause, Object message) {
        suspended = true;
        failedMessage = message;
        system.eventStream().publish(new Messages.ErrorEvent(self, cause, message));
        if (parent == null) {
            beginTermination();
        } else {
            parent.sendSystem(new SystemMessage.Failed(this, cause));
        }
    }

    private void resume() {
        if (state != State.RUNNING || pending != Pending.NONE) {
            return;
        }
        if (actor == null) {
            beginRestart(new IllegalStateException("actor " + path + " was resumed without a live instance"));
            return;
        }
        failedMessage = null;
        suspended = false;
        for (ActorCell child : failedChildren) {
            child.sendSystem(new SystemMessage.Resume());
        }
        failedChildren.clear();
    }

    private void beginRestart(Throwable cause) {
        if (state != State.RUNNING || pending == Pending.TERMINATE) {
            return;
        }
        suspended = true;
        pending = Pending.RESTART;
        restartCause = cause;
        if (actor != null) {
            try {
                actor.preRestart(cause, failedMessage);
            } catch (Throwable hookFailure) {
                system.eventStream().publish(new Messages.ErrorEvent(self, hookFailure, null));
            }
        }
        failedMessage = null;
        stopChildren();
        completePendingIfChildrenGone();
    }

    private void finishRestart() {
        pending = Pending.NONE;
        failedChildren.clear();
        Throwable cause = restartCause;
        restartCause = null;
        try {
            actor = newActorInstance();
            actor.postRestart(cause);
            suspended = false;
        } catch (Throwable failure) {
            fail(failure, null);
        }
    }

    private void beginTermination() {
        synchronized (children) {
            if (state == State.TERMINATING || state == State.TERMINATED) {
                return;
            }
            state = State.TERMINATING;
        }
        suspended = true;
        pending = Pending.TERMINATE;
        stopChildren();
        completePendingIfChildrenGone();
    }

    private void finishTermination() {
        pending = Pending.NONE;
        if (actor != null) {
            try {
                actor.postStop();
            } catch (Throwable hookFailure) {
                system.eventStream().publish(new Messages.ErrorEvent(self, hookFailure, null));
            }
        }
        actor = null;
        behaviors.clear();
        for (Envelope envelope : mailbox.close()) {
            system.publishDeadLetter(envelope.message(), envelope.sender(), self);
        }
        for (ActorRef target : watching) {
            if (target instanceof LocalActorRef local) {
                local.cell().sendSystem(new SystemMessage.Unwatch(self));
            }
        }
        watching.clear();
        state = State.TERMINATED;
        system.unregister(this);
        for (ActorRef watcher : watchers) {
            watcher.tell(new Messages.Terminated(self), self);
        }
        watchers.clear();
        drainAfterTermination();
        termination.complete(null);
        if (parent != null) {
            parent.sendSystem(new SystemMessage.ChildTerminated(this));
        }
    }

    private void completePendingIfChildrenGone() {
        if (pending == Pending.NONE) {
            return;
        }
        synchronized (children) {
            if (!children.isEmpty()) {
                return;
            }
        }
        if (pending == Pending.RESTART) {
            finishRestart();
        } else {
            finishTermination();
        }
    }

    private void stopChildren() {
        for (ActorCell child : childCells()) {
            child.sendSystem(new SystemMessage.Stop());
        }
    }

    private List<ActorCell> childCells() {
        synchronized (children) {
            return new ArrayList<>(children.values());
        }
    }

    private void childTerminated(ActorCell child) {
        synchronized (children) {
            children.remove(child.name, child);
        }
        restartStats.remove(child);
        failedChildren.remove(child);
        completePendingIfChildrenGone();
    }

    private void addWatcher(ActorRef watcher) {
        if (watcher != self) {
            watchers.add(watcher);
        }
    }

    private void handleChildFailure(ActorCell child, Throwable cause) {
        if (state != State.RUNNING || pending != Pending.NONE || child.parent != this
            || child.state() == State.TERMINATED) {
            return;
        }
        SupervisorStrategy strategy = props.supervisorStrategy();
        List<ActorCell> targets = strategy.scope() == SupervisorStrategy.Scope.ALL_FOR_ONE
            ? childCells()
            : List.of(child);
        switch (strategy.decide(cause)) {
            case RESUME -> child.sendSystem(new SystemMessage.Resume());
            case RESTART -> {
                SupervisorStrategy.RestartStats stats =
                    restartStats.computeIfAbsent(child, ignored -> new SupervisorStrategy.RestartStats());
                boolean permitted = strategy.permitRestart(stats, System.nanoTime());
                for (ActorCell target : targets) {
                    target.sendSystem(permitted ? new SystemMessage.Restart(cause) : new SystemMessage.Stop());
                }
            }
            case STOP -> {
                for (ActorCell target : targets) {
                    target.sendSystem(new SystemMessage.Stop());
                }
            }
            case ESCALATE -> {
                if (parent == null) {
                    child.sendSystem(new SystemMessage.Stop());
                } else {
                    failedChildren.add(child);
                    fail(cause, null);
                }
            }
        }
    }

    ActorRef spawnChild(Props childProps, String childName, boolean generated) {
        Objects.requireNonNull(childProps, "props");
        validateName(childName, generated);
        ActorCell child;
        synchronized (children) {
            if (state == State.TERMINATING || state == State.TERMINATED) {
                throw new IllegalStateException("cannot spawn '" + childName + "' under terminating actor " + path);
            }
            if (children.containsKey(childName)) {
                throw new InvalidActorNameException("actor name '" + childName + "' is not unique under " + path);
            }
            child = new ActorCell(system, this, childName, childProps);
            children.put(childName, child);
            system.register(child);
            child.sendSystem(new SystemMessage.Create());
        }
        return child.self;
    }

    private void validateName(String candidate, boolean generated) {
        if (candidate == null || candidate.isEmpty()) {
            throw new InvalidActorNameException("actor name must not be empty");
        }
        if (!generated && candidate.startsWith("$")) {
            throw new InvalidActorNameException("actor names starting with '$' are reserved: " + candidate);
        }
        for (int i = 0; i < candidate.length(); i++) {
            char c = candidate.charAt(i);
            if (c == '/' || Character.isWhitespace(c) || Character.isISOControl(c)) {
                throw new InvalidActorNameException("illegal character in actor name: '" + candidate + "'");
            }
        }
    }

    @Override
    public ActorRef self() {
        return self;
    }

    @Override
    public ActorRef sender() {
        Envelope envelope = current;
        return envelope == null ? system.deadLetters() : envelope.sender();
    }

    @Override
    public ActorRef parent() {
        return parent == null ? system.deadLetters() : parent.self;
    }

    @Override
    public ActorSystem system() {
        return system;
    }

    @Override
    public String name() {
        return name;
    }

    @Override
    public ActorRef spawn(Props childProps, String childName) {
        return spawnChild(childProps, childName, false);
    }

    @Override
    public ActorRef spawn(Props childProps) {
        return spawnChild(childProps, "$" + Long.toString(nameCounter.incrementAndGet(), 36), true);
    }

    @Override
    public List<ActorRef> children() {
        List<ActorRef> refs = new ArrayList<>();
        for (ActorCell child : childCells()) {
            refs.add(child.self);
        }
        return refs;
    }

    @Override
    public Optional<ActorRef> child(String childName) {
        synchronized (children) {
            ActorCell child = children.get(childName);
            return child == null ? Optional.empty() : Optional.of(child.self);
        }
    }

    @Override
    public void stop(ActorRef ref) {
        if (ref == self) {
            sendSystem(new SystemMessage.Stop());
            return;
        }
        if (ref instanceof LocalActorRef local && local.cell().parent == this) {
            local.cell().sendSystem(new SystemMessage.Stop());
            return;
        }
        throw new IllegalArgumentException("an actor can only stop itself or its children; use ActorSystem.stop for " + ref);
    }

    @Override
    public ActorRef watch(ActorRef ref) {
        if (!(ref instanceof LocalActorRef local)) {
            throw new IllegalArgumentException("only local actors can be watched: " + ref);
        }
        if (ref != self && watching.add(ref)) {
            local.cell().sendSystem(new SystemMessage.Watch(self));
        }
        return ref;
    }

    @Override
    public void unwatch(ActorRef ref) {
        if (watching.remove(ref) && ref instanceof LocalActorRef local) {
            local.cell().sendSystem(new SystemMessage.Unwatch(self));
        }
    }

    @Override
    public void become(Receive behavior) {
        become(behavior, true);
    }

    @Override
    public void become(Receive behavior, boolean discardOld) {
        Objects.requireNonNull(behavior, "behavior");
        if (discardOld && !behaviors.isEmpty()) {
            behaviors.pop();
        }
        behaviors.push(behavior);
    }

    @Override
    public void unbecome() {
        if (!behaviors.isEmpty()) {
            behaviors.pop();
        }
    }
}
