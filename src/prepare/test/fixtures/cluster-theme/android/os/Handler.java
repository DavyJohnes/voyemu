package android.os;

/** Records delayed work; the theme tests drive the property poll themselves. */
public class Handler {
    public boolean postDelayed(Runnable work, long delayMillis) {
        return true;
    }
}
