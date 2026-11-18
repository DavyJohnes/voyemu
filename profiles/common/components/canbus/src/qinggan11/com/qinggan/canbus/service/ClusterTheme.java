package com.qinggan.canbus.service;

import android.content.Context;
import android.os.IBinder;
import android.util.Log;

import com.qinggan.canbus.VehicleState;

import java.util.Map;

/**
 * Qinggan11 cluster theme: the QGAPI cluster service and the CAN theme fields share the persisted
 * selection; the cluster shows themes 1, 2 and 4 and their skins.
 */
final class ClusterTheme {
    private final ClusterThemeStore store;
    private volatile ClusterThemeBinder binder;

    ClusterTheme(Context context) {
        store =
                new ClusterThemeStore(
                        context.getSharedPreferences("voyemu-cluster-theme", Context.MODE_PRIVATE),
                        CanBusState.systemProp("persist.sys.qg.currenttheme", "default"));
    }

    static boolean validSkin(int value) {
        return value == 1 || value == 1001 || value >= 3001 && value <= 3003;
    }

    static boolean validMode(int value) {
        return value == 1 || value == 2 || value == 4;
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
     * Applies a valid CAN write of IPK_THEM_MODE or IPK_THEME_SKIN and reports both fields; a skin
     * selects its theme as ThemeUtils.getThemeIdBySkin.
     */
    boolean write(VehicleState field, int value, Map<VehicleState, Integer> changed) {
        if (field == VehicleState.IPK_THEM_MODE ? !validMode(value) : !validSkin(value)) {
            return false;
        }

        if (field == VehicleState.IPK_THEM_MODE) {
            store.setMode(value);
        } else {
            store.setModeAndSkin((value + 999) / 1000, value);
        }

        changed.put(VehicleState.IPK_THEM_MODE, store.mode());
        changed.put(VehicleState.IPK_THEME_SKIN, store.skin());

        return true;
    }

    /** Publishes the current theme to the cluster service subscribers. */
    void changed() {
        ClusterThemeBinder current = binder;

        if (current != null) {
            current.changed();
        }
    }

    /** Registers qg.cluster.service; a recreated service keeps the binder and its subscribers. */
    synchronized void register(CanBusStubService canbus) {
        if (binder == null) {
            binder = new ClusterThemeBinder(canbus);
        }

        try {
            Class.forName("android.os.ServiceManager")
                    .getMethod("addService", String.class, IBinder.class)
                    .invoke(null, "qg.cluster.service", binder);
        } catch (ReflectiveOperationException e) {
            Log.e("CanBusStub", "failed to register qg.cluster.service", e);
        }
    }
}
