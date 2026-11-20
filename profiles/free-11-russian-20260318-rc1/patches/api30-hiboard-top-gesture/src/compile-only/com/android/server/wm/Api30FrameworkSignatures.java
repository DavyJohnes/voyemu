package com.android.server.wm;

import android.graphics.Rect;
import android.graphics.Region;
import android.view.DisplayInfo;
import android.view.WindowManager;

import java.util.function.Predicate;

// Compile-only direct signatures, validated against the exact clean services.jar. Never merged into
// guest DEX.

class DisplayPolicy {
    static native WindowState access$200(DisplayPolicy policy);

    static native DisplayContent access$900(DisplayPolicy policy);

    static native SystemGesturesPointerEventListener access$1100(DisplayPolicy policy);

    static native void access$400(DisplayPolicy policy, WindowState target);
}

class DisplayContent {
    native int getDisplayId();

    native DisplayInfo getDisplayInfo();

    native WindowState getWindow(Predicate<WindowState> predicate);
}

class SystemGesturesPointerEventListener {
    protected native boolean currentGestureStartedInRegion(Region region);
}

class WindowState {
    boolean mHasSurface, mObscured;

    public native int getOwningUid();

    public native int getDisplayId();

    public native String getOwningPackage();

    public native WindowManager.LayoutParams getAttrs();

    public native Rect getFrameLw();

    public native boolean isVisibleLw();

    native void getVisibleBounds(Rect bounds);

    native void getTouchableRegion(Region region);
}
