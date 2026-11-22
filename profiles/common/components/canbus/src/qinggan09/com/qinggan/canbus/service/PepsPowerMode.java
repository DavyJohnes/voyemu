package com.qinggan.canbus.service;

import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/**
 * BCM PEPS power mode handler of the OEM qinggan09 Dongfeng component
 * (DongfengCanBusComponentImpl.onBCM_PEPSChangeData, frame 0x2C1): a changed power mode switches
 * ACC; ACC OFF saves the account kind, and ACC ON restores the ambient light, the privacy setting
 * and the drive mode packet of that account. Repeating the current power mode changes nothing.
 */
public final class PepsPowerMode {
    /** States the OEM setVehicleState saves to QGSettings that ACC ON restores. */
    private static final Set<String> SAVED =
            new HashSet<>(
                    Arrays.asList(
                            "DRIVING_MODE_SET",
                            "EPS_MODE_SET",
                            "PROP_MODE_SET",
                            "IB_MODE_SET",
                            "HUM_ENERGY_PTREGEN_LEVL",
                            "FCM_SW_REQ",
                            "IVI_PRIVACY_SETTING"));

    private final PowerModeVehicle mVehicle;
    private volatile int mAccState = -1;

    public PepsPowerMode(PowerModeVehicle vehicle) {
        mVehicle = vehicle;
    }

    /** The ACC of the handler, OFF=0, ON=2, -1 before the first power mode. */
    public int accState() {
        return mAccState;
    }

    /** Whether the OEM setVehicleState saves this written state to QGSettings at this ACC. */
    public static boolean saved(String state, int acc) {
        return SAVED.contains(state) || ("VEHICLE_AMBIENT_LIGHT_SWITCH".equals(state) && acc == 2);
    }

    /** Applies bcm_pepsPowerMode of the frame: 1 is ACC OFF, 2 is ACC ON. */
    public void onPowerMode(int power) {
        PowerModeVehicle v = mVehicle;
        int acc = power == 1 ? 0 : power;

        if (mAccState == acc) {
            return;
        }

        if (acc == 0) {
            mAccState = 0;
            v.setVehicleState("VEHICLE_AMBIENT_LIGHT_SWITCH", 1);
            v.saveInt("ACC_STATUS", 0);
            v.saveInt("VehicleAccountInfo", v.guestAccount() ? 1 : 2);
        } else if (acc == 2) {
            v.setVehicleState(
                    "VEHICLE_AMBIENT_LIGHT_SWITCH",
                    v.initVehicleLastStatus("VEHICLE_AMBIENT_LIGHT_SWITCH", 2));
            v.cacheVehicleState("HDC_SWITCH", 2);
            v.setVehicleState(
                    "IVI_PRIVACY_SETTING", v.initVehicleLastStatus("IVI_PRIVACY_SETTING", 1));
            Map<String, Integer> bundle = new LinkedHashMap<>();

            if (v.savedInt("VehicleAccountInfo", 1) == 1) {
                bundle.put("DRIVING_MODE_SET", 1);
                bundle.put("EPS_MODE_SET", 2);
                bundle.put("PROP_MODE_SET", 1);
                bundle.put("IB_MODE_SET", 1);
                bundle.put("HUM_ENERGY_PTREGEN_LEVL", 4);
            } else {
                int drive = v.initVehicleLastStatus("DRIVING_MODE_SET", 1);
                int eps = v.initVehicleLastStatus("EPS_MODE_SET", 2);
                int prop = v.initVehicleLastStatus("PROP_MODE_SET", 1);
                int ib = v.initVehicleLastStatus("IB_MODE_SET", 1);
                int regeneration = v.initVehicleLastStatus("HUM_ENERGY_PTREGEN_LEVL", 4);
                bundle.put("DRIVING_MODE_SET", drive);
                bundle.put("EPS_MODE_SET", eps);
                bundle.put("PROP_MODE_SET", prop);
                bundle.put("IB_MODE_SET", ib);
                bundle.put("HUM_ENERGY_PTREGEN_LEVL", regeneration);
            }

            v.setVehicleBundle(bundle);
            v.setVehicleState("FCM_SW_REQ", v.initVehicleLastStatus("FCM_SW_REQ", 2));
            v.setVehicleState("SREV_SOC_SET", v.initVehicleLastStatus("SREV_SOC_SET", 0));
            v.saveInt("ACC_STATUS", 2);
            mAccState = 2;
        } else {
            mAccState = 0;
        }

        v.accStateChanged(mAccState);
    }
}
