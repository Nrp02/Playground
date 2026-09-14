package di;

import java.util.List;

public final class CircularDependencyException extends ProvisionException {
    private static final long serialVersionUID = 1L;

    private final transient List<String> path;

    public CircularDependencyException(List<String> path) {
        super("Circular dependency: " + String.join(" -> ", path));
        this.path = List.copyOf(path);
    }

    public List<String> path() {
        return path;
    }
}
