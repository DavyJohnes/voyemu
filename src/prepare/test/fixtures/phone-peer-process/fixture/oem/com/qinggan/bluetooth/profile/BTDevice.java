package com.qinggan.bluetooth.profile;

import android.bluetooth.BluetoothDevice;
import android.content.Context;

/** Test double of the OEM device base, which keeps its context. */
public abstract class BTDevice implements com.qinggan.bluetooth.BTConst {
    protected Context mContext;

    protected BTDevice(Context context) {
        this.mContext = context;
    }

    public abstract String getAddress();

    public abstract int getBTClass();

    public abstract int getBTClassForAdapterHeadset();

    public int getBTType() {
        return 0;
    }

    public abstract String getName();

    public abstract BluetoothDevice getRaw();

    protected abstract void refreshAttr(BTDevice device);

    protected abstract void refreshName(BTDevice device);

    public void setPairingConfirmation(boolean confirm) {}

    public void setPin(int pin) {}
}
