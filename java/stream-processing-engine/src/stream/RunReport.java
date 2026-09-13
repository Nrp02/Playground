package stream;

import java.util.List;

public record RunReport<T, R>(List<WindowResult<R>> results, List<Event<T>> lateEvents, long eventsProcessed,
                              long eventsFiltered, int checkpoints, int recoveries, long discardedOutputs) {

    public RunReport {
        results = List.copyOf(results);
        lateEvents = List.copyOf(lateEvents);
    }
}
