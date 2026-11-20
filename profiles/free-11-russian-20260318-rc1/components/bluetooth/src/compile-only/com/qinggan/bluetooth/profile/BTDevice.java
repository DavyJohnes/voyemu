package com.qinggan.bluetooth.profile;

public abstract class BTDevice implements com.qinggan.bluetooth.BTConst {
    protected BTDevice(android.content.Context p0) {}

    public abstract String getAddress();

    public abstract int getBTClass();

    public abstract int getBTClassForAdapterHeadset();

    public abstract String getName();

    public abstract android.bluetooth.BluetoothDevice getRaw();

    protected abstract void refreshAttr(BTDevice p0);

    protected abstract void refreshName(BTDevice p0);
}
