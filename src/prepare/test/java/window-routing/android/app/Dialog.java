package android.app;

import android.view.Window;
import android.view.WindowManager;

public final class Dialog {
    private WindowManager mWindowManager;
    private final Window window;

    public Dialog(WindowManager manager, Window window) {
        mWindowManager = manager;
        this.window = window;
    }

    public Window getWindow() {
        return window;
    }

    public WindowManager manager() {
        return mWindowManager;
    }
}
