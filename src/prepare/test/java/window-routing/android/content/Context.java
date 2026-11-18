package android.content;

import android.hardware.display.DisplayManager;
import android.view.Display;
import android.view.WindowManager;

import java.util.Map;

public class Context {
    public static final String DISPLAY_SERVICE = "display", WINDOW_SERVICE = "window";
    public final DisplayManager displays;
    public final Map<Integer, WindowManager> managers;
    final String owner;
    final int id;

    public Context(String o, DisplayManager d, Map<Integer, WindowManager> m) {
        this(o, d, m, 0);
    }

    private Context(String o, DisplayManager d, Map<Integer, WindowManager> m, int i) {
        owner = o;
        displays = d;
        managers = m;
        id = i;
    }

    public Context getApplicationContext() {
        return this;
    }

    public String getPackageName() {
        return owner;
    }

    public Object getSystemService(String s) {
        return DISPLAY_SERVICE.equals(s) ? displays : managers.get(id);
    }

    public Context createDisplayContext(Display d) {
        return new Context(owner, displays, managers, d.getDisplayId());
    }
}
