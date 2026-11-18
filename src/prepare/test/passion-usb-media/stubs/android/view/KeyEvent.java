package android.view;

public class KeyEvent {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1, FLAG_CANCELED = 32;
    private final int action, key, flags;

    public KeyEvent(int a, int k, int f) {
        action = a;
        key = k;
        flags = f;
    }

    public int getAction() {
        return action;
    }

    public int getKeyCode() {
        return key;
    }

    public int getFlags() {
        return flags;
    }
}
