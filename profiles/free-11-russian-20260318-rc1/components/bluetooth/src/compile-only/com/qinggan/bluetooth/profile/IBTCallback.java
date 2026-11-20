package com.qinggan.bluetooth.profile;

public interface IBTCallback {
    void onConnectionStateChanged(BTDevice p0, int p1, int p2);

    void onRequestBondedDevices(java.util.Set<BTDevice> p0);
}
