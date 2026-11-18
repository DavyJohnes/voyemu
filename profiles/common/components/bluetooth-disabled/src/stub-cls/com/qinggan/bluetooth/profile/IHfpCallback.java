package com.qinggan.bluetooth.profile;

public interface IHfpCallback extends IBTProfileCallback {
    void on3WayCallStateChanged(int p0, int p1, String p2);

    void onAudioEstablished();

    void onAudioReleased();

    @Deprecated
    void onCallStateChanged(int p0, String p1);

    void onPhoneMessage(Object p0);

    void onRingFromPhone(boolean p0);
}
