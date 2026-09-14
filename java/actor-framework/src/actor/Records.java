package actor;

final class Records {

    private Records() {
    }

    static String describe(Object message) {
        String text = String.valueOf(message);
        return text.length() > 80 ? text.substring(0, 77) + "..." : text;
    }
}
