package com.qinggan.canbus;

/** Numbers independently checked against API30 MusicConcate donor initializer. */
public enum VehicleState {
    HUM_VSP_FUNCTION_SW(665),
    IVI_SOC_MODESET(957);
    private final int value;

    VehicleState(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }
}
