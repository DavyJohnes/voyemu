package android.util;

public class Log {
    public static int rejected;

    public static int d(String tag, String message) {
        return 0;
    }

    public static int w(String tag, String message, Throwable e) {
        return 0;
    }

    public static int e(String tag, String message, Throwable e) {
        throw new AssertionError(message, e);
    }

    public static int w(String tag, String message) {
        if (message.contains("unsupported")) {
            rejected++;
        }

        return 0;
    }
}
