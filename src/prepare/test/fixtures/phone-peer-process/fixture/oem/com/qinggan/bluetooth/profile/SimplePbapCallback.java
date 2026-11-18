package com.qinggan.bluetooth.profile;

/** Test double of the OEM PBAP callback base, which exposes the device being synchronized. */
public abstract class SimplePbapCallback implements IPbapCallback {
    protected BTDevice currentSyncDevice;

    public BTDevice getSyncDevice() {
        return currentSyncDevice;
    }
}
