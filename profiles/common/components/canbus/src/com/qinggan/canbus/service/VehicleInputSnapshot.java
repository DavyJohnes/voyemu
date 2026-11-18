package com.qinggan.canbus.service;

import java.util.Arrays;

/** Immutable complete OEM input revision; no Android or UI dependencies. */
final class VehicleInputSnapshot {
    static final int UNKNOWN = -9999;
    final long sequence;
    private final int[] values;

    VehicleInputSnapshot(long sequence, int[] values) {
        if (sequence < 1 || sequence > 0xffffffffL || values == null || values.length != 579) {
            throw new IllegalArgumentException("Complete vehicle revision required");
        }

        this.values = values.clone();

        for (int i = 0; i < values.length; i++) {
            if (this.values[i] < 0
                    || this.values[i] > VehicleSignalCatalog.MAXIMA[i]
                    || (VehicleSignalCatalog.NAMES[i].startsWith("swm_") && this.values[i] != 0)) {
                throw new IllegalArgumentException(
                        "Invalid OEM signal: " + VehicleSignalCatalog.NAMES[i]);
            }
        }

        if (raw("speed10x") != raw("speed") * 10 || raw("soc") > 100) {
            throw new IllegalArgumentException("Vehicle speed/SOC consistency");
        }

        this.sequence = sequence;
    }

    int raw(String name) {
        return values[VehicleSignalCatalog.index(name)];
    }

    int[] copyValues() {
        return values.clone();
    }

    boolean sameValues(VehicleInputSnapshot other) {
        return other != null && sequence == other.sequence && Arrays.equals(values, other.values);
    }

    VehicleInputSnapshot withLegacy(long next, int speed, int soc, int door, int left, int right) {
        int[] result = values.clone();
        String[] names = {"speed", "speed10x", "soc", "door_fl", "turn_left", "turn_right"};
        int[] updates = {speed, speed * 10, soc, door, left, right};

        for (int i = 0; i < names.length; i++) {
            result[VehicleSignalCatalog.index(names[i])] = updates[i];
        }

        return new VehicleInputSnapshot(next, result);
    }

    int tjaState() {
        return raw("mpc_icaSts");
    }

    int gear() {
        return raw("gear_valid") == 1 && raw("gear") <= 3 ? raw("gear") : -1;
    }

    int ambientC() {
        return raw("ambient_temp") / 2 - 40;
    }

    // QNX enginecore DataIDs279/280: preserve the wire sentinel, otherwise saturate at999km.
    int evRangeKm() {
        int n = raw("remain_mileage_e");

        return n == 2047 ? UNKNOWN : Math.min(n, 999);
    }

    int fuelRangeKm() {
        int n = raw("remain_mileage_f");

        return n == 1023 ? UNKNOWN : Math.min(n, 999);
    }

    int evRealRangeKm() {
        int n = raw("vcu_remainingMileageReal");

        return n == 2047 ? UNKNOWN : Math.min(n, 999);
    }

    int evCycleRangeKm() {
        int n = raw("vcu_remainingMileageCLTCWLTC");

        return n == 2047 ? UNKNOWN : Math.min(n, 999);
    }

    float tyrePressure(String corner) {
        if (raw("can_v1_5_bcm_tpms" + corner + "Sts") == 0
                || raw("bcm_tpms" + corner + "Sts") == 0) {
            return -1.0f;
        } // Original TPMSInfo unknown pressure value.

        return raw("bcm_tyre" + corner + "Pressure") / 10.0f;
    }

    int tyreWarning(String corner) {
        if (tyrePressure(corner) < 0) {
            return -1;
        }

        if (raw("bcm_tpms" + corner + "RAPID_LEAK_Warning") != 0) {
            return 3;
        }

        if (raw("bcm_tpms" + corner + "LOW_BATT_Warning") != 0) {
            return 5;
        }

        // High/low raw severity mapping has not been recovered; keep it unknown.
        return raw("bcm_PressureWarning" + corner) == 0 ? 0 : -1;
    }
}
