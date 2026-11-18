package com.qinggan.bluetooth.profile;

public interface IRfcommCallback {
    void onRead(byte[] p0, int p1, int p2);

    void onStateChanged(int p0);
}
