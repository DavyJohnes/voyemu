package android.view;

public class MotionEvent extends InputEvent {
    final int action;

    public MotionEvent(int action) {
        this.action = action;
    }

    public final int getAction() {
        return action;
    }
}
