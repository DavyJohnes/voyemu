package com.qinggan.bluetooth.profile;

public interface IA2dpCallback extends IBTProfileCallback {
    void onMediaInfoChanged(com.qinggan.bluetooth.bean.BTMediaInfo p0);

    void onMediaPathChanged(com.qinggan.bluetooth.bean.BTMediaInfo p0);

    void onPlayDurationChanged(long p0);

    void onPlayStateChanged(boolean p0);
}
