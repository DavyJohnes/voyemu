package com.qinggan.canbus;

public final class NaviInfo implements android.os.Parcelable {
    @Override
    public int describeContents() {
        return 0;
    }

    public String getCurrentRoadName() {
        return null;
    }

    public GuideState getGuideState() {
        return null;
    }

    public int getNextDistance() {
        return 0;
    }

    public int getTbtIconId() {
        return 0;
    }

    public String getTurnRoadName() {
        return null;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
