package di;

import java.util.List;

public final class ConfigurationException extends DiException {
    private static final long serialVersionUID = 1L;

    private final transient List<String> messages;

    public ConfigurationException(String message) {
        this(List.of(message));
    }

    public ConfigurationException(List<String> messages) {
        super(format(messages));
        this.messages = List.copyOf(messages);
    }

    public List<String> messages() {
        return messages;
    }

    private static String format(List<String> messages) {
        if (messages.size() == 1) {
            return messages.get(0);
        }
        StringBuilder out = new StringBuilder(messages.size() + " configuration errors:");
        for (int i = 0; i < messages.size(); i++) {
            out.append(System.lineSeparator()).append("  ").append(i + 1).append(") ").append(messages.get(i));
        }
        return out.toString();
    }
}
