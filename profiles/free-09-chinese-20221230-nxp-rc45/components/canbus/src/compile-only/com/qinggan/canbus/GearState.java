package com.qinggan.canbus;

public enum GearState implements android.os.Parcelable {
    Unknown;

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
