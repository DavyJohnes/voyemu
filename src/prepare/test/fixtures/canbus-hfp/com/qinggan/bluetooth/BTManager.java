package com.qinggan.bluetooth;

public class BTManager {
    public static final BTManager INSTANCE = new BTManager();
    public final com.qinggan.bluetooth.profile.BTAdapter adapter =
            new com.qinggan.bluetooth.profile.BTAdapter();

    public static BTManager getInstance(android.content.Context context) {
        return INSTANCE;
    }

    public com.qinggan.bluetooth.profile.BTAdapter getAdapter(
            com.qinggan.bluetooth.profile.IBTCallback callback) {
        if (callback != null) {
            throw new AssertionError();
        }

        return adapter;
    }
}
