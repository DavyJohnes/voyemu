package android.os;

public class Looper {
    public static final Looper MAIN = new Looper();
    public static Looper current = MAIN;

    public static Looper myLooper() {
        return current;
    }
}
