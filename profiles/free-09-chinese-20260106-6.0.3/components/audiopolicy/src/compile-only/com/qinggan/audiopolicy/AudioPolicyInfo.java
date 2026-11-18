package com.qinggan.audiopolicy;

public class AudioPolicyInfo implements android.os.Parcelable {
    String clientId;
    String packageName;
    int streamType;

    public AudioPolicyInfo() {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
