package di;

import java.lang.annotation.Annotation;
import java.util.Objects;

public final class Names {

    private Names() {
    }

    public static Named named(String value) {
        return new NamedLiteral(Objects.requireNonNull(value, "value"));
    }

    private static final class NamedLiteral implements Named {
        private final String value;

        NamedLiteral(String value) {
            this.value = value;
        }

        @Override
        public String value() {
            return value;
        }

        @Override
        public Class<? extends Annotation> annotationType() {
            return Named.class;
        }

        @Override
        public boolean equals(Object other) {
            return other instanceof Named named && value.equals(named.value());
        }

        @Override
        public int hashCode() {
            return (127 * "value".hashCode()) ^ value.hashCode();
        }

        @Override
        public String toString() {
            return "@di.Named(\"" + value + "\")";
        }
    }
}
