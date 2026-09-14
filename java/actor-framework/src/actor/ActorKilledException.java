package actor;

public final class ActorKilledException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public ActorKilledException(String path) {
        super("actor " + path + " received Kill");
    }
}
