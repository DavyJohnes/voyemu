package com.qinggan.bluetooth.profile;

public interface IHfpCallback {
    void on3WayCallStateChanged(int state, int id, String number);

    void onReady();
}
