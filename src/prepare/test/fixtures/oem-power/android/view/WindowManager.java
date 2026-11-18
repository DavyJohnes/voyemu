package android.view;

import java.util.LinkedHashMap;
import java.util.Map;

/** The windows of one display, by view. */
public class WindowManager {
    public static class LayoutParams {
        public static final int MATCH_PARENT = -1,
                TYPE_SYSTEM_ERROR = 2010,
                FLAG_NOT_FOCUSABLE = 8,
                FLAG_LAYOUT_IN_SCREEN = 256,
                FLAG_LAYOUT_NO_LIMITS = 512;
        public int width, height, type, flags, format;
        public CharSequence title = "";

        public LayoutParams(int width, int height, int type, int flags, int format) {
            this.width = width;
            this.height = height;
            this.type = type;
            this.flags = flags;
            this.format = format;
        }

        public final void setTitle(CharSequence value) {
            title = value;
        }
    }

    public final Display display;
    public final Map<View, LayoutParams> windows = new LinkedHashMap<>();

    public WindowManager(Display display) {
        this.display = display;
    }

    public void addView(View view, LayoutParams params) {
        if (windows.containsKey(view)) {
            throw new IllegalStateException("View already added");
        }

        windows.put(view, params);
    }

    public void removeView(View view) {
        if (windows.remove(view) == null) {
            throw new IllegalArgumentException("View not attached to window manager");
        }
    }
}
