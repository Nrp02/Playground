package actor;

public abstract class Actor {
    private ActorCell cell;

    protected Actor() {
    }

    final void bind(ActorCell owner) {
        if (cell != null) {
            throw new IllegalStateException("actor instance " + getClass().getName()
                + " is already bound; a Props factory must create a fresh instance each time");
        }
        cell = owner;
    }

    protected final ActorContext context() {
        if (cell == null) {
            throw new IllegalStateException("actor context is only available after the actor is spawned");
        }
        return cell;
    }

    protected final ActorRef self() {
        return context().self();
    }

    protected final ActorRef sender() {
        return context().sender();
    }

    public abstract void receive(Object message) throws Exception;

    public void preStart() throws Exception {
    }

    public void postStop() throws Exception {
    }

    public void preRestart(Throwable reason, Object message) throws Exception {
        postStop();
    }

    public void postRestart(Throwable reason) throws Exception {
        preStart();
    }

    protected void unhandled(Object message) {
        context().system().eventStream().publish(new Messages.UnhandledMessage(message, sender(), self()));
    }
}
