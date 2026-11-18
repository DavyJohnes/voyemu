package android.view;

public class ViewTreeObserver {
    public interface OnGlobalLayoutListener {
        void onGlobalLayout();
    }

    public final java.util.List<OnGlobalLayoutListener> listeners = new java.util.ArrayList<>();

    public boolean isAlive() {
        return true;
    }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener value) {
        listeners.add(value);
    }

    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener value) {
        listeners.remove(value);
    }

    public void layout() {
        for (OnGlobalLayoutListener value : new java.util.ArrayList<>(listeners)) {
            value.onGlobalLayout();
        }
    }
}
