package android.util;

public class Log {
    public static int errors;

    public static int e(String t, String m, Throwable e) {
        errors++;

        return 0;
    }
}
