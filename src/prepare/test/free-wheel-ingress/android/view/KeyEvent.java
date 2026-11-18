package android.view;

public class KeyEvent {
    private final int code, action, flags;

    public KeyEvent(int c, int a, int f) {
        code = c;
        action = a;
        flags = f;
    }

    public int getKeyCode() {
        return code;
    }

    public int getAction() {
        return action;
    }

    public int getFlags() {
        return flags;
    }

    public boolean isCanceled() {
        return (flags & 32) != 0;
    }
}
