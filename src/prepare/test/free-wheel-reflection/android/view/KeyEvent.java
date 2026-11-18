package android.view;

/** Own JVM test double, not Android implementation. */
public class KeyEvent {
    private final int code;
    private final boolean canceled;

    public KeyEvent(int code, boolean canceled) {
        this.code = code;
        this.canceled = canceled;
    }

    public int getKeyCode() {
        return code;
    }

    public boolean isCanceled() {
        return canceled;
    }
}
