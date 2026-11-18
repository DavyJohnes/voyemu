package com.qinggan.bluetooth.profile;

import java.util.Set;

public interface IBTCallback {
    void onAclDisconnected(BTDevice p0);

    void onBTStateChanged(int p0, int p1);

    void onConnectionStateChanged(BTDevice p0, int p1, int p2);

    void onDeviceAdded(BTDevice p0);

    void onDeviceAttrChange(BTDevice p0);

    void onDeviceBondStateChanged(BTDevice p0, int p1, int p2);

    void onDeviceDeleted(BTDevice p0);

    void onDeviceNameChange(BTDevice p0);

    void onLocalNameChanged(String p0);

    void onPairingRequest(BTDevice p0, int p1, int p2);

    void onReady();

    void onRequestBondedDevices(Set<BTDevice> p0);

    void onScanModeChanged(int p0, int p1);

    void onScanningStateChanged(boolean p0);
}
