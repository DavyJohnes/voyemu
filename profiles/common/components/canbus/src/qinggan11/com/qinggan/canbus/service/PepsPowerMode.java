package com.qinggan.canbus.service;

import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/**
 * BCM PEPS power mode handler of the OEM qinggan11 Dongfeng H97C component
 * (DongfengH97CCanBusComponentImpl.onBCM_PEPSChangeData, frame 0x2C1): it publishes the power mode;
 * a changed power mode switches ACC; ACC OFF saves the account kind, and ACC ON restores the
 * ambient light, the privacy setting and the drive mode packet of that account, publishes the drive
 * mode and selects its linked ambient light colour. Repeating the current power mode changes no
 * ACC. The H97Y and H97X vehicle types form the export branch.
 */
public final class PepsPowerMode {
    /** States the OEM setVehicleState saves to QGSettings that ACC ON restores or selects. */
    private static final Set<String> SAVED =
            new HashSet<>(
                    Arrays.asList(
                            "DRIVING_MODE_SET",
                            "EPS_MODE_SET",
                            "PROP_MODE_SET",
                            "HUM_ENERGY_PTREGEN_LEVL",
                            "FCM_SW_REQ",
                            "IVI_PRIVACY_SETTING",
                            "ICM_SCR_BRI_LEVEL",
                            "VEHICLE_AMBIENT_LIGHT_COLOR"));

    private static final int H97Y = 13401;
    private static final int H97X = 13402;
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

    private boolean export() {
        int type = mVehicle.vehicleTypeId();

        return type == H97Y || type == H97X;
    }

    /** Ambient light colour linked to a drive mode; 0 for a mode without one. */
    private static int modeColor(int drive) {
        switch (drive) {
            case 1:
                return 19;
            case 2:
                return 5;
            case 3:
                return 64;
            case 4:
                return 54;
            case 5:
                return 10;
            case 6:
                return 9;
            default:
                return 0;
        }
    }

    /** APA language of the system locale, as the OEM parseLanguageConfig. */
    private int languageConfig() {
        String language = mVehicle.property("persist.sys.locale", "");

        if (language.isEmpty()) {
            language = mVehicle.property("ro.product.locale", "");
        }

        boolean h97x = mVehicle.vehicleTypeId() == H97X;

        switch (language) {
            case "zh-CN":
                return 0;
            case "ar":
            case "ar-EG":
            case "ar-SA":
                return h97x ? 2 : 1;
            case "ru":
            case "ru-RU":
                return h97x ? 3 : 2;
            case "de":
            case "de-DE":
                return h97x ? 5 : 1;
            case "es":
            case "es-ES":
                return h97x ? 4 : 1;
            case "fr":
            case "fr-FR":
                return h97x ? 6 : 1;
            case "it":
            case "it-IT":
                return h97x ? 11 : 1;
            default:
                return 1;
        }
    }

    /** Applies bcm_pepsPowerMode of the frame: 1 is ACC OFF, 2 is ACC ON. */
    public void onPowerMode(int power) {
        PowerModeVehicle v = mVehicle;

        if (power != v.vehicleState("BCM_PEPS_POWER_MODE")) {
            v.cacheVehicleState("BCM_PEPS_POWER_MODE", power);
            v.saveInt("BCM_PEPS_POWER_MODE", power);
            v.publishVehicleState("BCM_PEPS_POWER_MODE", power);
        }

        int acc = power == 1 ? 0 : power;

        if (mAccState == acc) {
            return;
        }

        if (acc == 0) {
            mAccState = 0;
            v.setVehicleState("VEHICLE_AMBIENT_LIGHT_SWITCH", 1);

            if (v.property("com.qinggan.manualwashmode.wash_car.state", "off").equals("on")) {
                v.setVehicleState("CAR_CLEANING_MODE_SWITCH", 0);
            }

            v.saveInt("ACC_STATUS", 0);
            v.saveInt("VehicleAccountInfo", export() || v.guestAccount() ? 1 : 2);
        } else if (acc == 2) {
            v.setVehicleState(
                    "VEHICLE_AMBIENT_LIGHT_SWITCH",
                    v.initVehicleLastStatus("VEHICLE_AMBIENT_LIGHT_SWITCH", 2));
            v.cacheVehicleState("HDC_SWITCH", 2);
            v.setVehicleState(
                    "IVI_PRIVACY_SETTING", v.initVehicleLastStatus("IVI_PRIVACY_SETTING", 1));
            int linked = v.globalInt("vehicle_setting_drive_mode_atm", 0);
            Map<String, Integer> bundle = new LinkedHashMap<>();
            int drive;

            if (v.savedInt("VehicleAccountInfo", 1) == 1) {
                drive = 1;
                bundle.put("DRIVING_MODE_SET", 1);
                bundle.put("EPS_MODE_SET", 2);
                bundle.put("PROP_MODE_SET", 1);

                if (export()) {
                    bundle.put("HUM_ENERGY_PTREGEN_LEVL", 4);
                }
            } else {
                drive = v.initVehicleLastStatus("DRIVING_MODE_SET", 1);
                int eps = v.initVehicleLastStatus("EPS_MODE_SET", 2);
                int prop = v.initVehicleLastStatus("PROP_MODE_SET", 1);
                bundle.put("DRIVING_MODE_SET", drive);
                bundle.put("EPS_MODE_SET", eps);
                bundle.put("PROP_MODE_SET", prop);
            }

            v.setVehicleBundle(bundle);
            v.publishVehicleState("DRIVING_MODE_SET", drive);

            if (linked == 1 && modeColor(drive) != 0) {
                v.setVehicleState("VEHICLE_AMBIENT_LIGHT_COLOR", modeColor(drive));
            }

            v.setVehicleState("FCM_SW_REQ", v.initVehicleLastStatus("FCM_SW_REQ", 2));
            v.setVehicleState("ICM_SCR_BRI_LEVEL", v.initVehicleLastStatus("ICM_SCR_BRI_LEVEL", 5));
            v.setVehicleState("SREV_SOC_SET", v.initVehicleLastStatus("SREV_SOC_SET", 0));

            if (!export() && v.onlineConfig("MATRIXLIGHT") == 1) {
                v.setVehicleState("IHBCFunction", 2);
            }

            if (v.vehicleTypeId() == H97X) {
                v.setVehicleState("APA_LANGUAGE_SET", languageConfig());
            }

            v.saveInt("ACC_STATUS", 2);
            mAccState = 2;
        } else {
            mAccState = 0;
        }

        v.accStateChanged(mAccState);
    }
}
