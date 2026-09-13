package stream;

import java.util.Objects;

public record Event<T>(String key, T value, long timestamp) {

    public Event {
        Objects.requireNonNull(key, "key");
    }

    public static <T> Event<T> of(String key, T value, long timestamp) {
        return new Event<>(key, value, timestamp);
    }
}
