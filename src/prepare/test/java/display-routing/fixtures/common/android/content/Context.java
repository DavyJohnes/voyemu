package android.content;

import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.view.Display;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class Context {
    public static final String DISPLAY_SERVICE = "display", WINDOW_SERVICE = "window";
    public static final DisplayManager DISPLAYS = new DisplayManager();
    public static final Map<Integer, WindowManager> MANAGERS = new HashMap<>();
    public static final Map<Class<?>, Object> SERVICES = new HashMap<>();
    public static final List<Bundle> STARTS = new ArrayList<>();
    public final String owner;
    public final int id;

    public Context(String p, int i) {
        owner = p;
        id = i;
    }

    public Context getApplicationContext() {
        return this;
    }

    public String getPackageName() {
        return owner;
    }

    public Context createDisplayContext(Display d) {
        return new Context(owner, d.id);
    }

    public Object getSystemService(String s) {
        return DISPLAY_SERVICE.equals(s)
                ? DISPLAYS
                : MANAGERS.computeIfAbsent(id, i -> new WindowManager(i));
    }

    public <T> T getSystemService(Class<T> type) {
        return type.cast(type == DisplayManager.class ? DISPLAYS : SERVICES.get(type));
    }

    public void startActivity(Intent intent, Bundle options) {
        STARTS.add(options);
    }
}
