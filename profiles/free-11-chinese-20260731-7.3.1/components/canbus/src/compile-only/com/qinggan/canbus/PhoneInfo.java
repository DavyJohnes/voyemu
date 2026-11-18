package com.qinggan.canbus;

public final class PhoneInfo implements android.os.Parcelable {
    public PhoneInfo() {}

    @Override
    public int describeContents() {
        return 0;
    }

    public int getDuration() {
        return 0;
    }

    public String getName() {
        return null;
    }

    public String getPhoneNum() {
        return null;
    }

    public PhoneState getPhoneState() {
        return null;
    }

    public int isVehicleCall() {
        return 0;
    }

    public void setDuration(int p0) {}

    public void setName(String p0) {}

    public void setPhoneNum(String p0) {}

    public void setPhoneState(PhoneState p0) {}

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
