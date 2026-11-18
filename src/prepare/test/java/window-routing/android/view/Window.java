package android.view;

public final class Window {
    private WindowManager mWindowManager;
    private final WindowManager.LayoutParams attributes;
    private final View decor;

    public Window(WindowManager manager, WindowManager.LayoutParams attributes, View decor) {
        mWindowManager = manager;
        this.attributes = attributes;
        this.decor = decor;
    }

    public WindowManager.LayoutParams getAttributes() {
        return attributes;
    }

    public View getDecorView() {
        return decor;
    }

    public WindowManager manager() {
        return mWindowManager;
    }
}
