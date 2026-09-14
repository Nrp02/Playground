package actor;

import java.util.List;
import java.util.Optional;

public interface ActorContext {
    ActorRef self();

    ActorRef sender();

    ActorRef parent();

    ActorSystem system();

    String name();

    ActorRef spawn(Props props, String name);

    ActorRef spawn(Props props);

    List<ActorRef> children();

    Optional<ActorRef> child(String name);

    void stop(ActorRef ref);

    ActorRef watch(ActorRef ref);

    void unwatch(ActorRef ref);

    void become(Receive behavior);

    void become(Receive behavior, boolean discardOld);

    void unbecome();
}
