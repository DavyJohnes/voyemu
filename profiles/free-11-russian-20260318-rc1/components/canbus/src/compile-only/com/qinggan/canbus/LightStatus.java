package com.qinggan.canbus;

public final class LightStatus implements android.os.Parcelable {
    public int dayLight;
    public int dippedBeam;
    public int directionL;
    public int directionR;
    public int fogRear;
    public int mainBeam;
    public int positionLamp;

    public LightStatus() {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
