package com.android.server.wm;

import android.graphics.Rect;
import android.graphics.Region;
import android.view.DisplayInfo;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

class WindowState {
    int uid = 1000, display = 0;
    String pkg = "com.qinggan.app.hiboard";
    boolean visible = true, mHasSurface = true, mObscured = false;
    WindowManager.LayoutParams attrs = new WindowManager.LayoutParams();
    Rect frame = new Rect(145, 0, 1920, 50), touch = new Rect(145, 0, 1920, 50), visibleBounds;

    public int getOwningUid() {
        return uid;
    }

    public int getDisplayId() {
        return display;
    }

    public String getOwningPackage() {
        return pkg;
    }

    public WindowManager.LayoutParams getAttrs() {
        return attrs;
    }

    public Rect getFrameLw() {
        return frame;
    }

    public boolean isVisibleLw() {
        return visible;
    }

    void getTouchableRegion(Region region) {
        region.set(touch);
    }

    void getVisibleBounds(Rect out) {
        Rect b = visibleBounds == null ? frame : visibleBounds;

        out.left = b.left;
        out.top = b.top;
        out.right = b.right;
        out.bottom = b.bottom;
    }
}

class DisplayContent {
    int id;
    DisplayInfo info = new DisplayInfo();
    List<WindowState> windows = new ArrayList<>();
    boolean broken;

    int getDisplayId() {
        return id;
    }

    DisplayInfo getDisplayInfo() {
        return info;
    }

    WindowState getWindow(Predicate<WindowState> predicate) {
        if (broken) {
            throw new IllegalStateException("inventory unavailable");
        }

        for (WindowState w : windows) {
            if (predicate.test(w)) {
                return w;
            }
        }

        return null;
    }
}

class SystemGesturesPointerEventListener {
    int x = 960, y = 2;
    boolean broken;

    protected boolean currentGestureStartedInRegion(Region r) {
        if (broken) {
            throw new LinkageError("gesture contract unavailable");
        }

        return r.contains(x, y);
    }
}

class DisplayPolicy {
    WindowState status;
    DisplayContent display = new DisplayContent();
    SystemGesturesPointerEventListener gestures = new SystemGesturesPointerEventListener();
    int originalCalls;
    boolean originalThrows;

    static WindowState access$200(DisplayPolicy p) {
        return p.status;
    }

    static DisplayContent access$900(DisplayPolicy p) {
        return p.display;
    }

    static SystemGesturesPointerEventListener access$1100(DisplayPolicy p) {
        return p.gestures;
    }

    static void access$400(DisplayPolicy p, WindowState w) {
        p.originalCalls++;

        if (p.originalThrows) {
            throw new IllegalStateException("original side effect uncertain");
        }
    }
}
