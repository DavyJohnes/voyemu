package com.qinggan.bluetooth.profile;

public interface IHfpCallback extends IBTProfileCallback {
    void on3WayCallStateChanged(int p0, int p1, String p2);
}
