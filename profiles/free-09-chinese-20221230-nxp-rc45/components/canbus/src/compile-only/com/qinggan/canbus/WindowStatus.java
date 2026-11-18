package com.qinggan.canbus;

public final class WindowStatus implements android.os.Parcelable {
    public static final int UNKNOWN = -1;
    public int fLWindow;
    public int fLWindowHotProtect;
    public int fLWindowLearn;
    public int fLWindowPosition;
    public int fRWindow;
    public int fRWindowHotProtect;
    public int fRWindowLearn;
    public int fRWindowPosition;
    public int rLWindow;
    public int rLWindowHotProtect;
    public int rLWindowLearn;
    public int rLWindowPosition;
    public int rRWindow;
    public int rRWindowHotProtect;
    public int rRWindowLearn;
    public int rRWindowPosition;
    public int roll;
    public int rollPosition;
    public int sunroof;
    public int sunroofPosition;
    public int sunroofTilt;

    public WindowStatus() {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
