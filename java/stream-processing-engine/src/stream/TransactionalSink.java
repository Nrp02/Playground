package stream;

import java.util.ArrayList;
import java.util.List;

public final class TransactionalSink<E> {
    private final List<E> committed = new ArrayList<>();
    private final List<E> pending = new ArrayList<>();
    private int commits;
    private int rollbacks;
    private long discarded;

    public void write(E element) {
        pending.add(element);
    }

    public void writeAll(List<? extends E> elements) {
        pending.addAll(elements);
    }

    public void commit() {
        committed.addAll(pending);
        pending.clear();
        commits++;
    }

    public void rollback() {
        discarded += pending.size();
        pending.clear();
        rollbacks++;
    }

    public List<E> committed() {
        return List.copyOf(committed);
    }

    public int pendingCount() {
        return pending.size();
    }

    public int commitCount() {
        return commits;
    }

    public int rollbackCount() {
        return rollbacks;
    }

    public long discardedCount() {
        return discarded;
    }
}
