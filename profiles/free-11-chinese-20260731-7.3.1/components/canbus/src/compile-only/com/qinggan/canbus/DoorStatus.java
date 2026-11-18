package com.qinggan.canbus;

public final class DoorStatus implements android.os.Parcelable {
    public int bonnetDoor;
    public int fLDoor;
    public int fRDoor;
    public int loadSpace;
    public int rLDoor;
    public int rRDoor;

    public DoorStatus() {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
