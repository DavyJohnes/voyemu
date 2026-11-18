package com.voyemu.output;

import android.os.Build;
import android.view.WindowManager;

/** Publish the genuine OEM side dock as an inset without AOSP bottom-bar layout. */
public final class PassionSidebarWindow {
    private PassionSidebarWindow() {}

    public static void configure(WindowManager.LayoutParams window) {
        if (Build.VERSION.SDK_INT != 30
                || window == null
                || !"com.voyah.os.carlauncher".equals(window.packageName)
                || window.type != 2038
                || window.width != 140
                || window.height != -1) {
            return;
        }

        CharSequence title = window.getTitle();
        boolean primary =
                "ly-navigationBar".contentEquals(title == null ? "" : title)
                        && window.gravity == 8388611;
        boolean secondary =
                "ly-navigationBar-second".contentEquals(title == null ? "" : title)
                        && window.gravity == 8388613;

        if (!primary && !secondary) {
            return;
        }

        try {
            // API30 public @hide field; pinned DisplayPolicy consumes internal
            // ITYPE_NAVIGATION_BAR=1.
            WindowManager.LayoutParams.class
                    .getField("providesInsetsTypes")
                    .set(window, new int[] {1});
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException(
                    "Pinned API30 side-dock inset contract unavailable", failure);
        }

        // The source itself must span the display and must not fit its own published inset.
        window.setFitInsetsTypes(0);
    }
}
