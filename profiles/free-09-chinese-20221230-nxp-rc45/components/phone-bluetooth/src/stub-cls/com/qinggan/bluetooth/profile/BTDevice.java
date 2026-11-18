package com.qinggan.bluetooth.profile;

import android.content.Context;

public abstract class BTDevice implements com.qinggan.bluetooth.BTConst {
    protected Context mContext;

    public abstract String getAddress();

    public abstract int getBTClass();

    public int getBTType() {
        return 0;
    }

    public abstract String getName();

    protected abstract void refreshAttr(BTDevice p0);

    protected abstract void refreshName(BTDevice p0);

    public void setPairingConfirmation(boolean p0) {}

    public void setPin(int p0) {}

    protected BTDevice(Context p0) {}
}
