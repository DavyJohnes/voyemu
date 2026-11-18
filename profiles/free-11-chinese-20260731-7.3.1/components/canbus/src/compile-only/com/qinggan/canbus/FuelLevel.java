package com.qinggan.canbus;

public final class FuelLevel implements android.os.Parcelable {
    public float mAvgFuelConsumption;
    public float mInstantaneousFuelConsumption;
    public float mPercentage;

    public FuelLevel() {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
