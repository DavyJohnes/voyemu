package android.os;

public final class Looper {
    public static final Looper MAIN = new Looper();

    public static Looper myLooper() {
        return MAIN;
    }
}
