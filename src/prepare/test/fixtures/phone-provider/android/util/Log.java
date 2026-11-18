package android.util;

public class Log {
    public static final java.util.List<String> ENTRIES = new java.util.ArrayList<>();

    public static int i(String tag, String message) {
        ENTRIES.add(tag + " " + message);

        return 0;
    }
}
