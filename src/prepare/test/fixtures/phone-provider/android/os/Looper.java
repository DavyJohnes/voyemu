package android.os;

public class Looper {
    public static final Looper MAIN = new Looper();
    public static boolean isMain;

    public static Looper myLooper() {
        return isMain ? MAIN : null;
    }
}
