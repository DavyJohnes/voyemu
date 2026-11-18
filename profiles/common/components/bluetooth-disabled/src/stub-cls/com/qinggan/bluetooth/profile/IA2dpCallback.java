package com.qinggan.bluetooth.profile;

public interface IA2dpCallback extends IBTProfileCallback {
    void onMediaInfoChanged(Object p0);

    void onMediaPathChanged(Object p0);

    void onPlayDurationChanged(long p0);

    void onPlayStateChanged(boolean p0);
}
