package stream;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.function.Consumer;

public final class WindowOperator<T, A, R> {
    private static final Comparator<WindowResult<?>> EMIT_ORDER =
        Comparator.<WindowResult<?>>comparingLong(result -> result.window().end())
            .thenComparingLong(result -> result.window().start())
            .thenComparing(WindowResult::key);

    private final WindowAssigner assigner;
    private final Aggregator<T, A, R> aggregator;
    private final long allowedLateness;
    private final TreeMap<String, TreeMap<Window, Slot<A>>> state = new TreeMap<>();
    private long watermark = Long.MIN_VALUE;
    private long dropped;

    public WindowOperator(WindowAssigner assigner, Aggregator<T, A, R> aggregator, long allowedLateness) {
        if (allowedLateness < 0) {
            throw new IllegalArgumentException("allowed lateness must not be negative");
        }
        this.assigner = assigner;
        this.aggregator = aggregator;
        this.allowedLateness = allowedLateness;
    }

    public List<WindowResult<R>> onElement(Event<T> event, Consumer<Event<T>> lateOutput) {
        TreeMap<Window, Slot<A>> windows = state.computeIfAbsent(event.key(), key -> new TreeMap<>());
        List<WindowResult<R>> updates = new ArrayList<>();
        boolean accepted = false;
        for (Window assigned : assigner.assign(event.timestamp())) {
            Window target = assigner.isMerging() ? coverOverlapping(windows, assigned) : assigned;
            if (isExpired(target)) {
                continue;
            }
            Slot<A> slot = assigner.isMerging() ? collapse(windows, target) : windows.get(target);
            if (slot == null) {
                slot = new Slot<>(aggregator.createAccumulator(), 0);
                windows.put(target, slot);
            }
            slot.accumulator = aggregator.add(slot.accumulator, event.value());
            accepted = true;
            if (target.maxTimestamp() <= watermark) {
                slot.emissions++;
                updates.add(new WindowResult<>(event.key(), target, aggregator.result(slot.accumulator),
                    slot.emissions, true));
            }
        }
        if (windows.isEmpty()) {
            state.remove(event.key());
        }
        if (!accepted) {
            dropped++;
            lateOutput.accept(event);
        }
        return updates;
    }

    public List<WindowResult<R>> onWatermark(long newWatermark) {
        if (newWatermark <= watermark) {
            return List.of();
        }
        watermark = newWatermark;
        List<WindowResult<R>> fired = new ArrayList<>();
        Iterator<Map.Entry<String, TreeMap<Window, Slot<A>>>> keys = state.entrySet().iterator();
        while (keys.hasNext()) {
            Map.Entry<String, TreeMap<Window, Slot<A>>> keyEntry = keys.next();
            Iterator<Map.Entry<Window, Slot<A>>> windows = keyEntry.getValue().entrySet().iterator();
            while (windows.hasNext()) {
                Map.Entry<Window, Slot<A>> windowEntry = windows.next();
                Window window = windowEntry.getKey();
                Slot<A> slot = windowEntry.getValue();
                if (slot.emissions == 0 && window.maxTimestamp() <= watermark) {
                    slot.emissions = 1;
                    fired.add(new WindowResult<>(keyEntry.getKey(), window,
                        aggregator.result(slot.accumulator), 1, false));
                }
                if (isExpired(window)) {
                    windows.remove();
                }
            }
            if (keyEntry.getValue().isEmpty()) {
                keys.remove();
            }
        }
        fired.sort(EMIT_ORDER);
        return fired;
    }

    public long currentWatermark() {
        return watermark;
    }

    public long droppedCount() {
        return dropped;
    }

    public int openWindowCount() {
        return state.values().stream().mapToInt(Map::size).sum();
    }

    public List<Window> openWindows(String key) {
        TreeMap<Window, Slot<A>> windows = state.get(key);
        return windows == null ? List.of() : List.copyOf(windows.keySet());
    }

    public String describe() {
        return assigner.describe() + ", allowedLateness=" + allowedLateness;
    }

    public void snapshot(DataOutputStream out) throws IOException {
        Codec<A> codec = aggregator.codec();
        out.writeLong(watermark);
        out.writeLong(dropped);
        out.writeInt(state.size());
        for (Map.Entry<String, TreeMap<Window, Slot<A>>> keyEntry : state.entrySet()) {
            out.writeUTF(keyEntry.getKey());
            out.writeInt(keyEntry.getValue().size());
            for (Map.Entry<Window, Slot<A>> windowEntry : keyEntry.getValue().entrySet()) {
                out.writeLong(windowEntry.getKey().start());
                out.writeLong(windowEntry.getKey().end());
                out.writeInt(windowEntry.getValue().emissions);
                codec.write(out, windowEntry.getValue().accumulator);
            }
        }
    }

    public void restore(DataInputStream in) throws IOException {
        Codec<A> codec = aggregator.codec();
        state.clear();
        watermark = in.readLong();
        dropped = in.readLong();
        int keyCount = in.readInt();
        for (int k = 0; k < keyCount; k++) {
            String key = in.readUTF();
            int windowCount = in.readInt();
            TreeMap<Window, Slot<A>> windows = new TreeMap<>();
            for (int w = 0; w < windowCount; w++) {
                Window window = new Window(in.readLong(), in.readLong());
                int emissions = in.readInt();
                windows.put(window, new Slot<>(codec.read(in), emissions));
            }
            state.put(key, windows);
        }
    }

    private boolean isExpired(Window window) {
        if (watermark == Long.MIN_VALUE) {
            return false;
        }
        long cleanupTime = window.maxTimestamp() > Long.MAX_VALUE - allowedLateness
            ? Long.MAX_VALUE
            : window.maxTimestamp() + allowedLateness;
        return cleanupTime <= watermark;
    }

    private Window coverOverlapping(TreeMap<Window, Slot<A>> windows, Window assigned) {
        Window cover = assigned;
        boolean grew = true;
        while (grew) {
            grew = false;
            for (Window existing : windows.keySet()) {
                if (existing.intersects(cover) && !cover.equals(existing.cover(cover))) {
                    cover = existing.cover(cover);
                    grew = true;
                }
            }
        }
        return cover;
    }

    private Slot<A> collapse(TreeMap<Window, Slot<A>> windows, Window target) {
        Slot<A> merged = null;
        Iterator<Map.Entry<Window, Slot<A>>> iterator = windows.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Window, Slot<A>> entry = iterator.next();
            if (!entry.getKey().intersects(target)) {
                continue;
            }
            Slot<A> slot = entry.getValue();
            iterator.remove();
            if (merged == null) {
                merged = new Slot<>(slot.accumulator, slot.emissions);
            } else {
                merged.accumulator = aggregator.merge(merged.accumulator, slot.accumulator);
                merged.emissions = Math.max(merged.emissions, slot.emissions);
            }
        }
        if (merged != null) {
            if (target.maxTimestamp() > watermark) {
                merged.emissions = 0;
            }
            windows.put(target, merged);
        }
        return merged;
    }

    private static final class Slot<A> {
        private A accumulator;
        private int emissions;

        private Slot(A accumulator, int emissions) {
            this.accumulator = accumulator;
            this.emissions = emissions;
        }
    }
}
