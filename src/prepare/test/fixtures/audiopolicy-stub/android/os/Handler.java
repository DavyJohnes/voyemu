package android.os;

public class Handler {
    public Handler(Looper looper) {}

    public final boolean postDelayed(Runnable runnable, long delay) {
        runnable.run();

        return true;
    }
}
