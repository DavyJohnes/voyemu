package com.qinggan.canbus;

public final class Odometer implements android.os.Parcelable {
    public int mBeenTravelingMileage;
    public int mCanTravelMileage;
    public int mCanTravelMileage_real;
    public int mEVCruisingRang_real;
    public int mEVCruisingRange;
    public int mEVCruisingRange_wltc;

    public Odometer() {}

    @Override
    public int describeContents() {
        return 0;
    }

    public void setTotalTravelingMileage(float p0) {}

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
