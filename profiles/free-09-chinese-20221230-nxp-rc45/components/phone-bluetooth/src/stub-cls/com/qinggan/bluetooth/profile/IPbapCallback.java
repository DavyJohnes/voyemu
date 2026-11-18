package com.qinggan.bluetooth.profile;

import com.qinggan.bluetooth.bean.BTCallLogItem;
import com.qinggan.bluetooth.bean.BTContactItem;

public interface IPbapCallback extends IBTProfileCallback {
    void onCallLogItemCountUpdated(int p0, int p1);

    void onCallLogItemFetched(BTCallLogItem p0);

    void onContactItemCountUpdated(int p0, int p1);

    void onContactItemFetched(BTContactItem p0);

    void onSyncStateChanged(int p0, int p1);
}
