package android.os;

public class SystemClock {
    public static long now = 9000;

    public static long elapsedRealtime() {
        return now;
    }
}
