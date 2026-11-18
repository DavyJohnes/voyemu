package com.qinggan.canbus.service;

import java.util.HashMap;
import java.util.Map;

/** Persistent emulator climate model; defaults match A9, not physical vehicle telemetry. */
final class ClimateModel {
    static final int UNKNOWN = -9999;

    interface Store {
        Map<String, Integer> load();

        boolean save(Map<String, Integer> values);
    }

    private final Store store;
    private Map<String, Integer> values;

    ClimateModel(Store store) {
        this.store = store;
        values = new HashMap<>();

        for (Map.Entry<String, Integer> e : store.load().entrySet()) {
            if (!supported(e.getKey())) {
                continue;
            }

            validate(e.getKey(), e.getValue());
            values.put(e.getKey(), e.getValue());
        }
    }

    static boolean supported(String key) {
        if (key == null) {
            return false;
        }

        switch (key) {
            case "AC_POWER_SWITCH":
            case "AC_SWITCH":
            case "AC_DUAL":
            case "AC_DUAL_ALL":
            case "AC_AUTO":
            case "AC_RECIRC_AIR":
            case "AC_FRONT_DEFROST_SWITCH":
            case "AC_REAR_DEFROST_SWITCH":
            case "AC_LEFT_TEMP":
            case "AC_RIGHT_TEMP":
            case "AC_REAR_TEMP":
            case "AC_BLOWER":
            case "AC_FLOW_MODE":
            case "AC_WORKING_MODE":
            case "VENT_COOL_SET":
                return true;
            default:
                return false;
        }
    }

    synchronized int get(String key) {
        return !supported(key) ? UNKNOWN : values.containsKey(key) ? values.get(key) : initial(key);
    }

    static int initial(String key) {
        if (!supported(key)) {
            return UNKNOWN;
        }

        switch (key) {
            case "AC_POWER_SWITCH":
            case "AC_SWITCH":
            case "AC_DUAL":
            case "AC_DUAL_ALL":
            case "AC_BLOWER":
                return 1;
            case "AC_LEFT_TEMP":
            case "AC_RIGHT_TEMP":
            case "AC_REAR_TEMP":
                return 240;
            case "AC_WORKING_MODE":
                return 2;
            default:
                return 0;
        }
    }

    private static void validate(String key, Integer value) {
        if (!supported(key) || value == null) {
            throw new IllegalArgumentException("Unsupported climate state");
        }

        if (key.endsWith("_TEMP")) {
            requireRange(value, 155, 305, "Climate temperature");
        } else if (key.equals("AC_BLOWER")) {
            requireRange(value, 1, 8, "Climate blower");
        } else if (key.equals("AC_FLOW_MODE")) {
            requireRange(value, 0, 6, "Climate flow");
        } else if (key.equals("AC_WORKING_MODE") || key.equals("AC_RECIRC_AIR")) {
            requireRange(value, 0, 3, "Climate mode");
        } else {
            requireRange(value, 0, 1, "Climate switch");
        }
    }

    private static void requireRange(int value, int min, int max, String reason) {
        if (value < min || value > max) {
            throw new IllegalArgumentException(reason);
        }
    }

    synchronized boolean set(Map<String, Integer> patch) {
        Map<String, Integer> next = new HashMap<>(values);

        for (Map.Entry<String, Integer> e : patch.entrySet()) {
            validate(e.getKey(), e.getValue());
            next.put(e.getKey(), e.getValue());
        }

        int dual = next.containsKey("AC_DUAL") ? next.get("AC_DUAL") : initial("AC_DUAL");

        if (dual == 0) {
            int temp =
                    patch.containsKey("AC_LEFT_TEMP")
                            ? patch.get("AC_LEFT_TEMP")
                            : patch.containsKey("AC_RIGHT_TEMP")
                                    ? patch.get("AC_RIGHT_TEMP")
                                    : get("AC_LEFT_TEMP");
            next.put("AC_LEFT_TEMP", temp);
            next.put("AC_RIGHT_TEMP", temp);
        }

        if (!store.save(next)) {
            return false;
        }

        values = next;

        return true;
    }
}
