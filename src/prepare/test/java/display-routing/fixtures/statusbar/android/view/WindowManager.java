package android.view;

import java.util.IdentityHashMap;
import java.util.Map;

public class WindowManager {
    public static class InvalidDisplayException extends RuntimeException {}

    public static class LayoutParams extends ViewGroup.LayoutParams {
        public int type, flags = 0x1234;
        private String title;

        public void setTitle(String t) {
            title = t;
        }

        public CharSequence getTitle() {
            return title;
        }
    }

    public final int id;
    public final Map<View, ViewGroup.LayoutParams> attached = new IdentityHashMap<>();
    public int adds, removes, updates;

    public WindowManager(int i) {
        id = i;
    }

    public void addView(View v, ViewGroup.LayoutParams p) {
        if (v.parent != null) {
            throw new IllegalArgumentException("already attached");
        }

        attached.put(v, p);
        v.parent = this;
        adds++;
    }

    public void updateViewLayout(View v, ViewGroup.LayoutParams p) {
        if (!attached.containsKey(v)) {
            throw new IllegalArgumentException("not attached");
        }

        attached.put(v, p);
        updates++;
    }

    public void removeView(View v) {
        if (attached.remove(v) == null) {
            throw new IllegalArgumentException("already removed");
        }

        v.parent = null;
        removes++;
    }

    public void removeViewImmediate(View v) {
        removeView(v);
    }

    public void platformDisplayRemoval() {
        for (View v : attached.keySet()) {
            v.parent = null;
        }

        attached.clear();
    }
}
