package com.voyemu.window;

import android.view.View;

/** Only the pinned OEM one-key automotive mute controls depend on this manager. */
public final class PassionOneKeyMute {
    private PassionOneKeyMute() {}

    private static final int[] IDS = {0x7f0a00f6, 0x7f0a00f7, 0x7f0a0264};

    public static void refresh(View root, Object manager) {
        if (root == null) {
            return;
        }

        for (int id : IDS) {
            View control = root.findViewById(id);

            if (control != null) {
                control.setEnabled(manager != null);
            }
        }
    }
}
