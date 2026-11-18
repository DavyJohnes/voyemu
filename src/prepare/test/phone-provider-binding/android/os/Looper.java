package android.os;

/** Boundary model: Binder worker has no current Looper; application main does. */
public final class Looper {
    public static final Looper MAIN = new Looper();
    private static final ThreadLocal<Looper> CURRENT = new ThreadLocal<>();

    public static Looper myLooper() {
        return CURRENT.get();
    }

    public static void enterMain() {
        CURRENT.set(MAIN);
    }

    public static void leave() {
        CURRENT.remove();
    }
}
