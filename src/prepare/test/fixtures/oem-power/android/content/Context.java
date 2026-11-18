package android.content;

import android.hardware.display.DisplayManager;
import android.view.Display;
import android.view.WindowManager;

import java.util.HashMap;
import java.util.Map;

/** The application context and the context of each display, one window manager per display. */
public class Context {
    private final DisplayManager displays;
    private final Map<Integer, WindowManager> windows;
    private final Display display;

    public Context(DisplayManager displays) {
        this(displays, new HashMap<>(), null);
    }

    private Context(DisplayManager displays, Map<Integer, WindowManager> windows, Display display) {
        this.displays = displays;
        this.windows = windows;
        this.display = display;
    }

    public final <T> T getSystemService(Class<T> service) {
        if (service == DisplayManager.class) {
            return service.cast(displays);
        }

        if (service == WindowManager.class && display != null) {
            return service.cast(windowManager(display.getDisplayId()));
        }

        return null;
    }

    public Context createDisplayContext(Display target) {
        windows.computeIfAbsent(target.getDisplayId(), id -> new WindowManager(target));

        return new Context(displays, windows, target);
    }

    /** The window manager of the display, created on the first display context. */
    public WindowManager windowManager(int id) {
        return windows.get(id);
    }
}
