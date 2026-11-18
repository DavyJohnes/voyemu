package com.qinggan.canbus;

public enum AirConditionState implements android.os.Parcelable {
    AC_AUTO,
    AC_BLOWER,
    AC_DUAL,
    AC_DUAL_ALL,
    AC_FLOW_MODE,
    AC_FRONT_DEFROST_SWITCH,
    AC_LEFT_TEMP,
    AC_POWER_SWITCH,
    AC_REAR_DEFROST_SWITCH,
    AC_REAR_TEMP,
    AC_RECIRC_AIR,
    AC_RIGHT_TEMP,
    AC_SWITCH,
    AC_WORKING_MODE,
    VENT_COOL_SET;

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
