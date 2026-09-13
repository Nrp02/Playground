package stream;

import java.util.List;

public interface WindowAssigner {

    List<Window> assign(long timestamp);

    boolean isMerging();

    String describe();
}
