package com.qinggan.bluetooth.profile;

/** Test double of the OEM HFP callback base, which exposes its current device. */
public abstract class SimpleHfpCallback implements IHfpCallback {
    protected BTDevice currentDevice;

    public BTDevice getCurrentDevice() {
        return currentDevice;
    }
}
