package com.voyemu.oem;

import android.content.SharedPreferences;
import android.util.Pair;

/** Persist a navigation tip decision before the user can close the process. */
public final class NaviConsent {
    private NaviConsent() {}

    public static boolean tryPersist(SharedPreferences preferences, Object update) {
        if (preferences == null || !(update instanceof Pair)) {
            return false;
        }

        Pair<?, ?> pair = (Pair<?, ?>) update;

        if (!(pair.first instanceof String) || !(pair.second instanceof Boolean)) {
            return false;
        }

        String key = (String) pair.first;

        if (!"pref.show_permit".equals(key) && !"pref.show_permit_check".equals(key)) {
            return false;
        }

        return preferences.edit().putBoolean(key, (Boolean) pair.second).commit();
    }
}
