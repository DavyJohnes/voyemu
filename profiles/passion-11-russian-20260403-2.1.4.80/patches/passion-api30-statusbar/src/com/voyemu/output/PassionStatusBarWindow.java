package com.voyemu.output;

import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

/** Keep the hidden AOSP window attached without claiming the genuine OEM status-bar slot. */
public final class PassionStatusBarWindow {
    private PassionStatusBarWindow() {}

    static boolean target(String owner, int type, String title) {
        return "com.android.systemui".equals(owner)
                && (type == 2000 || type == 2041)
                && "StatusBar".equals(title);
    }

    private static void collapse(View view, ViewGroup.LayoutParams params) {
        if (!(params instanceof WindowManager.LayoutParams)) {
            return;
        }

        WindowManager.LayoutParams window = (WindowManager.LayoutParams) params;
        CharSequence title = window.getTitle();

        if (!target(
                view.getContext().getPackageName(),
                window.type,
                title == null ? null : title.toString())) {
            return;
        }

        // API30 TYPE_STATUS_BAR_ADDITIONAL keeps the lifecycle without the singleton
        // TYPE_STATUS_BAR slot.
        window.type = 2041;
        window.height = 0;
        view.setVisibility(View.GONE);
    }

    public static void addView(WindowManager manager, View view, ViewGroup.LayoutParams params) {
        collapse(view, params);
        manager.addView(view, params);
    }

    public static void updateViewLayout(
            WindowManager manager, View view, ViewGroup.LayoutParams params) {
        collapse(view, params);
        manager.updateViewLayout(view, params);
    }
}
