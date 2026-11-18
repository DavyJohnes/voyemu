package com.qinggan.bluetooth.profile;

public interface IPbapCallback extends IBTProfileCallback {
    void onCallLogItemCountUpdated(int p0, int p1);

    void onCallLogItemFetched(Object p0);

    void onContactItemCountUpdated(int p0, int p1);

    void onContactItemFetched(Object p0);

    void onSyncStateChanged(int p0, int p1);
}
