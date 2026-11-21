package com.qinggan.canbus.service;

import java.util.Arrays;

/**
 * Immutable complete OEM input revision of the vehicle field catalogue of the platform; no Android
 * or UI dependencies.
 */
final class VehicleInputSnapshot {
    static final int UNKNOWN = -9999;
    final long sequence;
    private final long[] values;

    VehicleInputSnapshot(long sequence, long[] values) {
        if (sequence < 1
                || sequence > 0xffffffffL
                || values == null
                || values.length != VehicleSignalCatalog.NAMES.length) {
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

    /** The value of a field that the component reads; each such field has at most 31 bits. */
    int raw(String name) {
        return Math.toIntExact(value(name));
    }

    /** The value of any field of the catalogue, up to the 38 bits of the widest one. */
    long value(String name) {
        return values[VehicleSignalCatalog.index(name)];
    }

    /** Whether the value of the field is the all-ones of its width, an unknown OEM range. */
    private boolean allOnes(String name) {
        return values[VehicleSignalCatalog.index(name)]
                == VehicleSignalCatalog.MAXIMA[VehicleSignalCatalog.index(name)];
    }

    boolean sameValues(VehicleInputSnapshot other) {
        return other != null && sequence == other.sequence && Arrays.equals(values, other.values);
    }

    VehicleInputSnapshot withLegacy(long next, int speed, int soc, int door, int left, int right) {
        long[] result = values.clone();
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

    // QNX enginecore DataIDs279/280: preserve the all-ones wire sentinel of the slot, 10 bits for
    // the qinggan09 ICE range and 11 bits for the others, otherwise saturate at999km.
    private int rangeKm(String name) {
        return allOnes(name) ? UNKNOWN : Math.min(raw(name), 999);
    }

    int evRangeKm() {
        return rangeKm("remain_mileage_e");
    }

    int fuelRangeKm() {
        return rangeKm("remain_mileage_f");
    }

    int evRealRangeKm() {
        return rangeKm("vcu_remainingMileageReal");
    }

    int evCycleRangeKm() {
        return rangeKm("vcu_remainingMileageCLTCWLTC");
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
