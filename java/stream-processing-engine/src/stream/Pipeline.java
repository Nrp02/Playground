package stream;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.TreeSet;
import java.util.function.Predicate;

public final class Pipeline<T, A, R> {
    private final List<Event<T>> source;
    private final List<Predicate<Event<T>>> filters;
    private final WindowAssigner assigner;
    private final Aggregator<T, A, R> aggregator;
    private final long allowedLateness;
    private final long maxOutOfOrderness;
    private final int checkpointInterval;
    private final List<Long> failures;
    private final int retainedCheckpoints;
    private CheckpointStore store;

    private Pipeline(Builder<T> builder, Aggregator<T, A, R> aggregator) {
        this.source = List.copyOf(builder.source);
        this.filters = List.copyOf(builder.filters);
        this.assigner = Objects.requireNonNull(builder.assigner, "window assigner not configured");
        this.aggregator = aggregator;
        this.allowedLateness = builder.allowedLateness;
        this.maxOutOfOrderness = builder.maxOutOfOrderness;
        this.checkpointInterval = builder.checkpointInterval;
        this.failures = List.copyOf(builder.failures);
        this.retainedCheckpoints = builder.retainedCheckpoints;
        this.store = new CheckpointStore(retainedCheckpoints);
    }

    public static <T> Builder<T> from(List<Event<T>> source) {
        return new Builder<>(source);
    }

    public CheckpointStore checkpointStore() {
        return store;
    }

    public RunReport<T, R> run() {
        store = new CheckpointStore(retainedCheckpoints);
        TransactionalSink<WindowResult<R>> sink = new TransactionalSink<>();
        TransactionalSink<Event<T>> lateSink = new TransactionalSink<>();
        TreeSet<Long> pendingFailures = new TreeSet<>(failures);
        WindowOperator<T, A, R> operator = new WindowOperator<>(assigner, aggregator, allowedLateness);
        BoundedOutOfOrdernessWatermarks watermarks = new BoundedOutOfOrdernessWatermarks(maxOutOfOrderness);
        long nextCheckpointId = 0;
        int checkpoints = 0;
        int recoveries = 0;

        store.save(new Checkpoint(nextCheckpointId++, 0, snapshot(watermarks, operator)));
        checkpoints++;

        long offset = 0;
        while (offset < source.size()) {
            if (pendingFailures.remove(offset)) {
                recoveries++;
                Checkpoint checkpoint = store.latest();
                operator = new WindowOperator<>(assigner, aggregator, allowedLateness);
                watermarks = new BoundedOutOfOrdernessWatermarks(maxOutOfOrderness);
                restore(checkpoint, watermarks, operator);
                sink.rollback();
                lateSink.rollback();
                offset = checkpoint.sourceOffset();
                continue;
            }
            Event<T> event = source.get((int) offset);
            if (accepts(event)) {
                watermarks.onEvent(event.timestamp());
                sink.writeAll(operator.onElement(event, lateSink::write));
                sink.writeAll(operator.onWatermark(watermarks.currentWatermark()));
            }
            offset++;
            if (offset % checkpointInterval == 0) {
                store.save(new Checkpoint(nextCheckpointId++, offset, snapshot(watermarks, operator)));
                sink.commit();
                lateSink.commit();
                checkpoints++;
            }
        }

        sink.writeAll(operator.onWatermark(Long.MAX_VALUE));
        sink.commit();
        lateSink.commit();
        long accepted = source.stream().filter(this::accepts).count();
        return new RunReport<>(sink.committed(), lateSink.committed(), accepted, source.size() - accepted,
            checkpoints, recoveries, sink.discardedCount() + lateSink.discardedCount());
    }

    private boolean accepts(Event<T> event) {
        for (Predicate<Event<T>> filter : filters) {
            if (!filter.test(event)) {
                return false;
            }
        }
        return true;
    }

    private byte[] snapshot(BoundedOutOfOrdernessWatermarks watermarks, WindowOperator<T, A, R> operator) {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        try (DataOutputStream out = new DataOutputStream(buffer)) {
            watermarks.snapshot(out);
            operator.snapshot(out);
        } catch (IOException e) {
            throw new CheckpointException("failed to snapshot operator state", e);
        }
        return buffer.toByteArray();
    }

    private void restore(Checkpoint checkpoint, BoundedOutOfOrdernessWatermarks watermarks,
                         WindowOperator<T, A, R> operator) {
        try (DataInputStream in = checkpoint.payloadStream()) {
            watermarks.restore(in);
            operator.restore(in);
            if (in.available() != 0) {
                throw new CheckpointException("checkpoint " + checkpoint.id() + " has trailing state bytes");
            }
        } catch (IOException e) {
            throw new CheckpointException("failed to restore checkpoint " + checkpoint.id(), e);
        }
    }

    public static final class Builder<T> {
        private final List<Event<T>> source;
        private final List<Predicate<Event<T>>> filters = new ArrayList<>();
        private final List<Long> failures = new ArrayList<>();
        private WindowAssigner assigner;
        private long allowedLateness;
        private long maxOutOfOrderness;
        private int checkpointInterval = 100;
        private int retainedCheckpoints = 3;

        private Builder(List<Event<T>> source) {
            this.source = List.copyOf(source);
        }

        public Builder<T> filter(Predicate<Event<T>> predicate) {
            filters.add(predicate);
            return this;
        }

        public Builder<T> window(WindowAssigner windowAssigner) {
            this.assigner = windowAssigner;
            return this;
        }

        public Builder<T> allowedLateness(long lateness) {
            this.allowedLateness = lateness;
            return this;
        }

        public Builder<T> maxOutOfOrderness(long bound) {
            this.maxOutOfOrderness = bound;
            return this;
        }

        public Builder<T> checkpointEvery(int events) {
            if (events <= 0) {
                throw new IllegalArgumentException("checkpoint interval must be positive");
            }
            this.checkpointInterval = events;
            return this;
        }

        public Builder<T> retainCheckpoints(int count) {
            this.retainedCheckpoints = count;
            return this;
        }

        public Builder<T> failAt(long... offsets) {
            for (long offset : offsets) {
                failures.add(offset);
            }
            return this;
        }

        public <A, R> Pipeline<T, A, R> aggregate(Aggregator<T, A, R> aggregator) {
            return new Pipeline<>(this, aggregator);
        }
    }
}
