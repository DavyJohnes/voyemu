package android.os;

public class Looper {
    private static final Looper MAIN = new Looper();
    public static boolean main = true;

    public static Looper getMainLooper() {
        return MAIN;
    }

    public static Looper myLooper() {
        return main ? MAIN : null;
    }
}
