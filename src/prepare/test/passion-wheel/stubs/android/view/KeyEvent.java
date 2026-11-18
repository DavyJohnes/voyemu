package android.view;

public class KeyEvent {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1;
    public int action, key, flags;

    public KeyEvent(int a, int k) {
        action = a;
        key = k;
    }

    public static KeyEvent changeFlags(KeyEvent e, int flags) {
        e.flags = flags;

        return e;
    }
}
