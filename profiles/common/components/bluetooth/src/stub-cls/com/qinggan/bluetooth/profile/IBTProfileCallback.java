package com.qinggan.bluetooth.profile;

interface IBTProfileCallback {
    void onReady();

    void onStateChanged(BTDevice p0, int p1, int p2);
}
