package com.qinggan.canbus.service;

import android.content.ContentResolver;
import android.util.Log;

import java.util.Map;

/**
 * What the OEM BCM PEPS power mode handler of {@link PepsPowerMode} reads and changes: the OEM
 * QGSettings, account and vehicle configuration through the guest QGAPI and the vehicle states of
 * the component. Vehicle states are named by their {@code VehicleState} constant.
 */
public abstract class PowerModeVehicle {
    private static final String TAG = "PowerModeVehicle";
    private final ContentResolver mResolver;

    protected PowerModeVehicle(ContentResolver resolver) {
        mResolver = resolver;
    }

    /** QGSettings.System.getInt; the default without QGSettings. */
    public int savedInt(String key, int value) {
        try {
            return (Integer)
                    Class.forName("com.qinggan.provider.QGSettings$System")
                            .getMethod("getInt", ContentResolver.class, String.class, int.class)
                            .invoke(null, mResolver, key, value);
        } catch (ReflectiveOperationException | LinkageError absent) {
            Log.w(TAG, "QGSettings unavailable; " + key + " keeps " + value);

            return value;
        }
    }

    /** QGSettings.System.putInt. */
    public void saveInt(String key, int value) {
        try {
            Class.forName("com.qinggan.provider.QGSettings$System")
                    .getMethod("putInt", ContentResolver.class, String.class, int.class)
                    .invoke(null, mResolver, key, value);
        } catch (ReflectiveOperationException | LinkageError absent) {
            Log.w(TAG, "QGSettings unavailable; " + key + "=" + value + " not saved");
        }
    }

    /** QGSettings.Global.getInt; the default without QGSettings. */
    public int globalInt(String key, int value) {
        try {
            return (Integer)
                    Class.forName("com.qinggan.provider.QGSettings$Global")
                            .getMethod("getInt", ContentResolver.class, String.class, int.class)
                            .invoke(null, mResolver, key, value);
        } catch (ReflectiveOperationException | LinkageError absent) {
            Log.w(TAG, "QGSettings unavailable; global " + key + " keeps " + value);

            return value;
        }
    }

    /** AccountUserManager.getInstance().AccountInfoBean().isGuest; a guest without QGAPI. */
    public boolean guestAccount() {
        try {
            Class<?> manager = Class.forName("com.qinggan.account.AccountUserManager");
            Object account =
                    manager.getMethod("AccountInfoBean")
                            .invoke(manager.getMethod("getInstance").invoke(null));

            return account == null || account.getClass().getField("isGuest").getBoolean(account);
        } catch (ReflectiveOperationException | LinkageError absent) {
            Log.w(TAG, "AccountUserManager unavailable; guest account assumed");

            return true;
        }
    }

    private static Class<?> vehicleClass(String name) throws ClassNotFoundException {
        return Class.forName(
                name,
                true,
                new dalvik.system.PathClassLoader(
                        "/system/framework/QGVehicle.jar",
                        PowerModeVehicle.class.getClassLoader()));
    }

    /** VehicleConfigHelper.getVehicleTypeID; -1 without QGVehicle. */
    public int vehicleTypeId() {
        try {
            return (Integer)
                    vehicleClass("com.qinggan.vehicle.VehicleConfigHelper")
                            .getMethod("getVehicleTypeID")
                            .invoke(null);
        } catch (ReflectiveOperationException | LinkageError absent) {
            Log.w(TAG, "VehicleConfigHelper unavailable; vehicle type unknown");

            return -1;
        }
    }

    /** VehicleConfigHelper.readOnlineConfigItem(VehicleOnlineState.item); -1 without QGVehicle. */
    @SuppressWarnings({"unchecked", "rawtypes"})
    public int onlineConfig(String item) {
        try {
            Class<?> helper = vehicleClass("com.qinggan.vehicle.VehicleConfigHelper");
            Class state =
                    Class.forName(
                            "com.qinggan.vehicle.VehicleOnlineState",
                            true,
                            helper.getClassLoader());

            return (Integer)
                    helper.getMethod("readOnlineConfigItem", state)
                            .invoke(null, Enum.valueOf(state, item));
        } catch (ReflectiveOperationException | LinkageError | IllegalArgumentException absent) {
            Log.w(TAG, "VehicleConfigHelper unavailable; " + item + " unknown");

            return -1;
        }
    }

    /** SystemProperties.get. */
    public String property(String key, String value) {
        try {
            return (String)
                    Class.forName("android.os.SystemProperties")
                            .getMethod("get", String.class, String.class)
                            .invoke(null, key, value);
        } catch (ReflectiveOperationException | LinkageError absent) {
            return value;
        }
    }

    /** getVehicleState. */
    public abstract int vehicleState(String state);

    /** initVehicleLastStatus: caches and returns the saved value of a state. */
    public abstract int initVehicleLastStatus(String state, int value);

    /** Caches a state value without a command. */
    public abstract void cacheVehicleState(String state, int value);

    /** setVehicleState. */
    public abstract void setVehicleState(String state, int value);

    /** setVehicleAndAirConditionBundleState(null, bundle) in the order of the map. */
    public abstract void setVehicleBundle(Map<String, Integer> bundle);

    /** onVehicleStateChanged to the observers. */
    public abstract void publishVehicleState(String state, int value);

    /** onAccStateChanged to the observers. */
    public abstract void accStateChanged(int acc);
}
