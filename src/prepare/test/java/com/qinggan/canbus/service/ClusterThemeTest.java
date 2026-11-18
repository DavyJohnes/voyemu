package com.qinggan.canbus.service;

import android.content.Context;

import com.qinggan.canbus.VehicleState;

import java.util.HashMap;
import java.util.Map;

/** The qinggan11 cluster theme follows the theme mode the OEM apps write without skin support. */
public final class ClusterThemeTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) {
        Context context = new Context();
        ClusterTheme theme = new ClusterTheme(context);

        check(theme.mode() == 2 && theme.skin() == 1001, "fresh machine seeds the default theme");
        Map<VehicleState, Integer> changed = new HashMap<>();
        check(theme.write(VehicleState.IPK_THEM_MODE, 4, changed), "Speedy Dash accepted");
        check(theme.mode() == 4 && theme.skin() == 3001, "mode selects its default skin");
        check(
                changed.get(VehicleState.IPK_THEM_MODE) == 4
                        && changed.get(VehicleState.IPK_THEME_SKIN) == 3001,
                "both fields reported");
        changed.clear();
        check(theme.write(VehicleState.IPK_THEME_SKIN, 3002, changed), "skin accepted");
        check(theme.mode() == 4 && theme.skin() == 3002, "skin selects its theme");
        check(
                !theme.write(VehicleState.IPK_THEM_MODE, 5, new HashMap<>())
                        && !theme.write(VehicleState.IPK_THEME_SKIN, 4001, new HashMap<>()),
                "the cluster has no theme 5");
        check(theme.mode() == 4 && theme.skin() == 3002, "rejected writes keep the theme");
        check(theme.write(VehicleState.IPK_THEM_MODE, 1, new HashMap<>()), "theme 1 accepted");
        ClusterTheme restarted = new ClusterTheme(context);
        check(restarted.mode() == 1 && restarted.skin() == 1, "selection survives a restart");
        System.out.println("ClusterThemeTest PASS mode, skin, rejection, restart");
    }
}
