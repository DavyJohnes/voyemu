package com.qinggan.canbus;

public class TPMSInfo implements android.os.Parcelable {
    public float mLeftFrontTirePressureValue;
    public int mLeftFrontTireWarningStatus;
    public float mLeftRearTirePressureValue;
    public int mLeftRearTireWarningStatus;
    public float mRightFrontTirePressureValue;
    public int mRightFrontTireWarningStatus;
    public float mRightRearTirePressureValue;
    public int mRightRearTireWarningStatus;

    public TPMSInfo() {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
