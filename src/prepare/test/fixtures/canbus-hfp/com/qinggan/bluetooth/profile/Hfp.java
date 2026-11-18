package com.qinggan.bluetooth.profile;

public class Hfp {
    public IHfpCallback callback;
    public int registrations, removals;

    // Unregisters only the same callback instance, as the OEM profile does.
    @SuppressWarnings("ReferenceEquality")
    public void unregisterListener(IHfpCallback v) {
        if (callback == v) {
            callback = null;
            removals++;
        }
    }
}
