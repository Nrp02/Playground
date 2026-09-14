package actor;

import java.util.Objects;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

public final class EventStream {
    private final CopyOnWriteArrayList<Subscription<?>> subscriptions = new CopyOnWriteArrayList<>();
    private final AtomicBoolean subscriberFailed = new AtomicBoolean();

    public <T> Cancellable subscribe(Class<T> type, Consumer<? super T> handler) {
        Subscription<T> subscription = new Subscription<>(Objects.requireNonNull(type, "type"),
            Objects.requireNonNull(handler, "handler"), this);
        subscriptions.add(subscription);
        return subscription;
    }

    public void publish(Object event) {
        for (Subscription<?> subscription : subscriptions) {
            subscription.offer(event);
        }
    }

    public int subscriberCount() {
        return subscriptions.size();
    }

    private static final class Subscription<T> implements Cancellable {
        private final Class<T> type;
        private final Consumer<? super T> handler;
        private final EventStream owner;
        private final AtomicBoolean cancelled = new AtomicBoolean();

        Subscription(Class<T> type, Consumer<? super T> handler, EventStream owner) {
            this.type = type;
            this.handler = handler;
            this.owner = owner;
        }

        void offer(Object event) {
            if (!cancelled.get() && type.isInstance(event)) {
                try {
                    handler.accept(type.cast(event));
                } catch (RuntimeException ignored) {
                    owner.publishSubscriberFailure();
                }
            }
        }

        @Override
        public boolean cancel() {
            boolean first = cancelled.compareAndSet(false, true);
            owner.subscriptions.remove(this);
            return first;
        }

        @Override
        public boolean isCancelled() {
            return cancelled.get();
        }
    }

    private void publishSubscriberFailure() {
        subscriberFailed.set(true);
    }

    public boolean hadSubscriberFailure() {
        return subscriberFailed.get();
    }
}
