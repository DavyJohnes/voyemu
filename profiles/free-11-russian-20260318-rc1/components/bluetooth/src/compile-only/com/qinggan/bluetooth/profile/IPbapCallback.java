package com.qinggan.bluetooth.profile;

public interface IPbapCallback extends IBTProfileCallback {
    void onCallLogItemCountUpdated(int p0, int p1);

    void onContactItemCountUpdated(int p0, int p1);

    void onContactItemFetched(com.qinggan.bluetooth.bean.BTContactItem p0);

    void onSyncStateChanged(int p0, int p1);
}
