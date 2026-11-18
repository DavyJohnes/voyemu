package com.qinggan.bluetooth.profile;

public interface IHidCallback {
    void onConnectionStateChanged(BTDevice p0, int p1, int p2);

    void onReady();
}
