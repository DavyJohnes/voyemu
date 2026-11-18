package android.view;

import android.content.Context;

public class View {
    public interface OnClickListener {
        void onClick(View view);
    }

    public interface OnAttachStateChangeListener {
        void onViewAttachedToWindow(View view);

        void onViewDetachedFromWindow(View view);
    }

    public final java.util.List<OnAttachStateChangeListener> attachListeners =
            new java.util.ArrayList<>();

    public void addOnAttachStateChangeListener(OnAttachStateChangeListener value) {
        attachListeners.add(value);
    }

    public void removeOnAttachStateChangeListener(OnAttachStateChangeListener value) {
        attachListeners.remove(value);
    }

    public void detach() {
        for (OnAttachStateChangeListener value : new java.util.ArrayList<>(attachListeners)) {
            value.onViewDetachedFromWindow(this);
        }
    }

    public final Context context;
    public ViewGroup parent;
    public View tab;
    public int width, height;
    public float x, y;
    public CharSequence description;
    public OnClickListener click;
    public final ViewTreeObserver observer = new ViewTreeObserver();

    public View(Context context) {
        this.context = context;
    }

    public Context getContext() {
        return context;
    }

    public ViewTreeObserver getViewTreeObserver() {
        return observer;
    }

    public View findViewById(int id) {
        return id == 7 ? tab : null;
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }

    public float getX() {
        return x;
    }

    public float getY() {
        return y;
    }

    public void setX(float value) {
        x = value;
    }

    public void setY(float value) {
        y = value;
    }

    public Object getParent() {
        return parent;
    }

    public void setContentDescription(CharSequence value) {
        description = value;
    }

    public void setOnClickListener(OnClickListener value) {
        click = value;
    }

    public void setBackgroundColor(int value) {}
}
