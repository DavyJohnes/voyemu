package android.os;

public final class SystemClock {
    public static long now = 1;

    public static long elapsedRealtime() {
        return now;
    }
}
