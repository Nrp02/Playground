import di.ConfigurationException;
import di.Key;
import di.Named;
import di.Names;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

public final class KeyTest {

    @Retention(RetentionPolicy.RUNTIME)
    @interface NotAQualifier {
    }

    static final class Holder {
        @Named("db.url")
        String url;

        @NotAQualifier
        String plain;
    }

    private KeyTest() {
    }

    public static void run() {
        Assertions.suite("Key");

        Assertions.test("keys with the same type and no qualifier are equal", () -> {
            Assertions.assertEquals("equal", Key.of(String.class), Key.of(String.class));
            Assertions.assertEquals("hash", Key.of(String.class).hashCode(), Key.of(String.class).hashCode());
        });

        Assertions.test("qualifier distinguishes otherwise identical keys", () -> {
            Assertions.assertFalse("named vs plain", Key.of(String.class).equals(Key.named(String.class, "a")));
            Assertions.assertFalse("different names", Key.named(String.class, "a").equals(Key.named(String.class, "b")));
        });

        Assertions.test("Names.named literal equals a reflected @Named annotation", () -> {
            try {
                Named reflected = Holder.class.getDeclaredField("url").getAnnotation(Named.class);
                Named literal = Names.named("db.url");
                Assertions.assertEquals("literal equals reflected", reflected, literal);
                Assertions.assertTrue("reflected equals literal", literal.equals(reflected));
                Assertions.assertEquals("hash codes agree", reflected.hashCode(), literal.hashCode());
                Assertions.assertEquals("keys agree", Key.of(String.class, reflected), Key.named(String.class, "db.url"));
            } catch (NoSuchFieldException e) {
                throw new AssertionError(e);
            }
        });

        Assertions.test("primitive types are boxed", () -> {
            Assertions.assertEquals("int", Key.of(Integer.class), Key.of(int.class));
            Assertions.assertEquals("type", Integer.class, Key.of(int.class).type());
        });

        Assertions.test("non-qualifier annotations are rejected", () -> {
            Assertions.assertThrows("rejected", ConfigurationException.class, () -> {
                try {
                    Key.of(String.class, Holder.class.getDeclaredField("plain").getAnnotation(NotAQualifier.class));
                } catch (NoSuchFieldException e) {
                    throw new AssertionError(e);
                }
            });
        });

        Assertions.test("void keys are rejected", () ->
            Assertions.assertThrows("void", ConfigurationException.class, () -> Key.of(void.class)));

        Assertions.test("toString shows the qualifier", () -> {
            Assertions.assertEquals("plain", "String", Key.of(String.class).toString());
            Assertions.assertEquals("named", "@Named(\"port\") Integer", Key.named(int.class, "port").toString());
        });
    }
}
