package com.qinggan.bluetooth.profile;

public abstract class Pbap implements IBTProfile {
    protected android.os.Handler mHandler;

    protected Pbap(android.content.Context p0, com.qinggan.bluetooth.BTDeviceManager p1) {}

    public abstract boolean cancelSync();

    public abstract int getDataResponseType();

    public void registerListener(IPbapCallback p0) {}

    public void registerListener(IPbapCallback p0, String p1) {}

    public abstract boolean startCallLogSync();

    public abstract boolean startCallLogSyncByRange(int p0, int p1);

    public abstract boolean startContactsSync();

    public abstract boolean startContactsSyncByRange(int p0, int p1);

    public void unregisterListener(IPbapCallback p0) {}

    public void unregisterListener(IPbapCallback p0, String p1) {}
}
