package com.qinggan.canbus.service;

import android.content.SharedPreferences;

/**
 * The cluster retains the user's CAN theme selection across service and guest restarts. Without
 * persist.app.skin.support the OEM apps select a theme by its mode; the cluster shows it in the
 * default skin of ThemeUtils.getDefaultSkinId.
 */
final class ClusterThemeStore {
    private final SharedPreferences preferences;
    private int mode;
    private int skin;

    ClusterThemeStore(SharedPreferences preferences, String theme) {
        this.preferences = preferences;
        int initialMode = "simple".equals(theme) ? 4 : "golden".equals(theme) ? 5 : 2;
        mode = preferences.getInt("mode", initialMode);
        skin = preferences.getInt("skin", defaultSkin(initialMode));
    }

    static int defaultSkin(int mode) {
        return mode == 1 ? 1 : mode >= 2 && mode <= 5 ? (mode - 1) * 1000 + 1 : -1;
    }

    int mode() {
        return mode;
    }

    int skin() {
        return skin;
    }

    int[] snapshot() {
        return new int[] {mode, skin};
    }

    /** Selects a theme by its mode; a mode without an OEM default skin keeps the skin. */
    void setMode(int value) {
        int next = defaultSkin(value);

        save(value, next < 0 ? skin : next);
    }

    void setSkin(int value) {
        save(mode, value);
    }

    void setModeAndSkin(int nextMode, int nextSkin) {
        save(nextMode, nextSkin);
    }

    private void save(int nextMode, int nextSkin) {
        if (!preferences.edit().putInt("mode", nextMode).putInt("skin", nextSkin).commit()) {
            throw new IllegalStateException("Cannot persist cluster theme selection");
        }

        mode = nextMode;
        skin = nextSkin;
    }
}
