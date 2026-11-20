package com.qinggan.system;

public class AreaFocusModel implements android.os.Parcelable {
    public int areaId;
    public int bottomFocusArea;
    public int leftFocusArea;
    public String packageName;
    public int rightFocusArea;
    public int topFocusArea;

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
