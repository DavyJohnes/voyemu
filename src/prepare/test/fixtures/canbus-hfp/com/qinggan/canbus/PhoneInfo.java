package com.qinggan.canbus;

public class PhoneInfo {
    public PhoneState state;
    public int duration;
    public String name, number;

    public void setPhoneState(PhoneState v) {
        state = v;
    }

    private int isVehicleCall;

    public void setVehicleCall(int v) {
        isVehicleCall = v;
    }

    public int isVehicleCall() {
        return isVehicleCall;
    }

    public void setDuration(int v) {
        duration = v;
    }

    public void setName(String v) {
        name = v;
    }

    public void setPhoneNum(String v) {
        number = v;
    }
}
