package com.qinggan.canbus.service;

import android.content.Context;

import com.qinggan.canbus.VehicleState;

import java.util.Map;

/**
 * Qinggan09 cluster theme: the CAN selection persists; OEM theme properties seed a fresh machine.
 */
final class ClusterTheme {
    private final ClusterThemeStore store;

    ClusterTheme(Context context) {
        store =
                new ClusterThemeStore(
                        context.getSharedPreferences("voyemu-cluster-theme", Context.MODE_PRIVATE),
                        CanBusState.systemProp("persist.sys.qg.currenttheme", "default"));
    }

    int mode() {
        return store.mode();
    }

    int skin() {
        return store.skin();
    }

    int[] snapshot() {
        return store.snapshot();
    }

    /**
     * Stores a CAN write of IPK_THEM_MODE or IPK_THEME_SKIN and reports the changed fields; a skin
     * write keeps the mode.
     */
    boolean write(VehicleState field, int value, Map<VehicleState, Integer> changed) {
        if (field == VehicleState.IPK_THEM_MODE) {
            store.setMode(value);
            changed.put(VehicleState.IPK_THEME_SKIN, store.skin());
        } else {
            store.setSkin(value);
        }

        changed.put(field, value);

        return true;
    }

    /** The qinggan09 cluster has no QGAPI cluster service to notify. */
    void changed() {}

    /** The qinggan09 cluster has no QGAPI cluster service to register. */
    void register(CanBusStubService canbus) {}
}
