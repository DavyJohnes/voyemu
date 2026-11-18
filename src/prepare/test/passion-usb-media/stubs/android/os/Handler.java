package android.os;

public class Handler {
    public Runnable pending;

    public Handler(Looper looper) {}

    public boolean postDelayed(Runnable r, long delay) {
        pending = r;

        return true;
    }

    public void removeCallbacks(Runnable r) {
        if (pending == r) {
            pending = null;
        }
    }
}
