package com.qinggan.bluetooth.profile;

public abstract class BTAdapter implements com.qinggan.bluetooth.BTConst {
    protected A2dp mA2dp;
    protected android.os.Handler mHandler;
    protected Hfp mHfp;
    protected boolean mIsReady;
    protected java.util.List<IBTCallback> mListeners;
    protected Pbap mPbap;

    protected BTAdapter(android.content.Context p0, com.qinggan.bluetooth.BTDeviceManager p1) {}

    public abstract boolean cancelDiscovery();

    public abstract boolean connect(BTDevice p0);

    public boolean connect(String p0) {
        return false;
    }

    public abstract boolean createBond(BTDevice p0);

    public abstract boolean disable();

    public abstract boolean disconnect(BTDevice p0);

    public boolean disconnect(String p0) {
        return false;
    }

    public abstract boolean enable();

    public abstract boolean enableAutoBond();

    public abstract A2dp getA2dp(IA2dpCallback p0);

    public abstract String getAddress();

    public abstract int getBondState(BTDevice p0);

    public java.util.List<BTDevice> getBondedDevices() {
        return null;
    }

    public abstract BTDevice getConnectedDevice();

    public abstract int getConnectionState();

    public abstract int getConnectionState(BTDevice p0);

    public Hfp getHfp(IHfpCallback p0) {
        return null;
    }

    public int getHfpState(String p0) {
        return 0;
    }

    public abstract String getName();

    public abstract Pbap getPbap(IPbapCallback p0);

    public BTDevice getRemoteDevice(String p0) {
        return null;
    }

    public abstract Rfcomm getRfcommForClient(
            android.content.Context p0, IRfcommCallback p1, BTDevice p2, int p3, boolean p4);

    public abstract Rfcomm getRfcommForClient(
            android.content.Context p0,
            IRfcommCallback p1,
            BTDevice p2,
            java.util.UUID p3,
            boolean p4);

    public abstract Rfcomm getRfcommForServer(
            android.content.Context p0, IRfcommCallback p1, int p2, boolean p3);

    public abstract Rfcomm getRfcommForServer(
            android.content.Context p0,
            IRfcommCallback p1,
            String p2,
            java.util.UUID p3,
            boolean p4);

    public abstract int getScanMode();

    public abstract Spp getSpp(ISppCallback p0);

    public abstract int getState();

    public abstract boolean isDiscovering();

    public abstract boolean isEnabled();

    public abstract boolean removeBond(BTDevice p0);

    public abstract void requestBondedDevices();

    public abstract void setDiscoverableTimeout(int p0);

    public abstract void setName(String p0);

    public abstract void setScanMode(int p0);

    public abstract boolean setScanMode(int p0, int p1);

    public abstract boolean startDiscovery();
}
