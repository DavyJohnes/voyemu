package com.voyemu.window;

import android.view.View;
import android.view.ViewGroup;

import java.util.WeakHashMap;

/** Keep unavailable ambient controls disabled without changing light state. */
public final class PassionAmbientLight {
    private static final WeakHashMap<View, Boolean> PREVIOUS = new WeakHashMap<>();

    private PassionAmbientLight() {}

    public static synchronized void refresh(View root, int mode) {
        if (root == null) {
            return;
        }

        View toggle = root.findViewById(0x7f0a044f), settings = root.findViewById(0x7f0a0273);

        if (mode < 0) {
            disable(toggle);
            disable(settings);
        } else {
            restore(toggle);
            restore(settings);
        }
    }

    private static void disable(View view) {
        if (view == null) {
            return;
        }

        if (!PREVIOUS.containsKey(view)) {
            PREVIOUS.put(view, view.isEnabled());
        }

        view.setEnabled(false);

        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;

            for (int i = 0; i < group.getChildCount(); i++) {
                disable(group.getChildAt(i));
            }
        }
    }

    private static void restore(View view) {
        if (view == null) {
            return;
        }

        Boolean enabled = PREVIOUS.remove(view);

        if (enabled != null) {
            view.setEnabled(enabled);
        }

        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;

            for (int i = 0; i < group.getChildCount(); i++) {
                restore(group.getChildAt(i));
            }
        }
    }
}
