package actor;

sealed interface SystemMessage {

    record Create() implements SystemMessage {
    }

    record Stop() implements SystemMessage {
    }

    record Resume() implements SystemMessage {
    }

    record Restart(Throwable cause) implements SystemMessage {
    }

    record Failed(ActorCell child, Throwable cause) implements SystemMessage {
    }

    record ChildTerminated(ActorCell child) implements SystemMessage {
    }

    record Watch(ActorRef watcher) implements SystemMessage {
    }

    record Unwatch(ActorRef watcher) implements SystemMessage {
    }
}
