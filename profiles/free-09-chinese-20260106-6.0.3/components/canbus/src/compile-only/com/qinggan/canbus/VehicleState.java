package com.qinggan.canbus;

public enum VehicleState implements android.os.Parcelable {
    DRIVING_MODE_SET,
    FRONT_SEAT_HEATING_COMMAND_LEFT,
    FRONT_SEAT_HEATING_COMMAND_RIGHT,
    FRONT_SEAT_HEATING_SWITCH_LEFT,
    FRONT_SEAT_HEATING_SWITCH_RIGHT,
    FRONT_SEAT_MASS_INTEN_LEFT,
    FRONT_SEAT_MASS_INTEN_RIGHT,
    FRONT_SEAT_MASS_SWITCH_LEFT,
    FRONT_SEAT_MASS_SWITCH_RIGHT,
    FRONT_SEAT_VENTILATION_COMMAND_LEFT,
    FRONT_SEAT_VENTILATION_COMMAND_RIGHT,
    FRONT_SEAT_VENTILATION_SWITCH_LEFT,
    FRONT_SEAT_VENTILATION_SWITCH_RIGHT,
    HUM_VSP_FUNCTION_SW,
    IPK_THEME_SKIN,
    IPK_THEM_MODE,
    IVI_SOC_MODESET,
    TJA_HWA_STS;

    @Override
    public int describeContents() {
        return 0;
    }

    public int getValue() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
