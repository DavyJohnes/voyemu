package android.view;

import android.graphics.Point;
import android.util.DisplayMetrics;

public class Display {
    public static final int DEFAULT_DISPLAY = 0, INVALID_DISPLAY = -1;
    public final int id;
    public String name, uniqueId;
    public int width = 1920, height = 720, density = 160;

    public Display(int i, String n) {
        id = i;
        name = n;
    }

    public int getDisplayId() {
        return id;
    }

    public String getName() {
        return name;
    }

    public String getUniqueId() {
        return uniqueId;
    }

    public void getRealSize(Point p) {
        p.x = width;
        p.y = height;
    }

    public void getRealMetrics(DisplayMetrics m) {
        m.densityDpi = density;
    }
}
