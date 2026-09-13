package stream;

public record WindowResult<R>(String key, Window window, R value, int emission, boolean late) {

    public boolean isUpdate() {
        return emission > 1;
    }

    @Override
    public String toString() {
        String suffix = late ? " (late, emission " + emission + ")" : "";
        return key + " " + window + " -> " + value + suffix;
    }
}
