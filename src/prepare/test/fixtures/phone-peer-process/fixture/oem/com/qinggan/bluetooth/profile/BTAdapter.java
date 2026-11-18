package com.qinggan.bluetooth.profile;

import android.content.Context;
import android.os.Handler;

import com.qinggan.bluetooth.BTDeviceManager;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/** Test double of the OEM adapter base: it keeps its listeners and profiles. */
public abstract class BTAdapter implements com.qinggan.bluetooth.BTConst {
    protected Context mContext;
    protected BTDeviceManager mDeviceManager;
    protected Handler mHandler;
    protected static BTAdapter sInstance;
    protected List<IBTCallback> mListeners = new CopyOnWriteArrayList<>();
    protected boolean mIsReady;
    protected Hfp mHfp;
    protected A2dp mA2dp;
    protected Pbap mPbap;
    protected Spp mSpp;
    protected Hid hid;

    public abstract boolean cancelDiscovery();

    public abstract boolean connect(BTDevice p0);

    public boolean connect(String p0) {
        return false;
    }

    public abstract boolean createBond(BTDevice p0);

    public abstract boolean disable();

    public boolean disableBTRecord(int p0) {
        return false;
    }

    public abstract boolean disconnect(BTDevice p0);

    public boolean disconnect(String p0) {
        return false;
    }

    public boolean disconnect(String p0, int p1) {
        return false;
    }

    public abstract boolean enable();

    public abstract boolean enableAutoBond();

    public boolean enableBTRecord(int p0, String p1) {
        return false;
    }

    public abstract A2dp getA2dp(IA2dpCallback p0);

    public int getA2dpState(String p0) {
        return 0;
    }

    public abstract String getAddress();

    public abstract int getBondState(BTDevice p0);

    public BTDevice getConnectedAudioDevice() {
        return null;
    }

    public abstract BTDevice getConnectedDevice();

    public abstract int getConnectionState();

    public abstract int getConnectionState(BTDevice p0);

    public int getHfpState(String p0) {
        return 0;
    }

    public abstract String getName();

    public abstract Pbap getPbap(IPbapCallback p0);

    public BTDevice getRemoteDevice(String p0) {
        return null;
    }

    public abstract Rfcomm getRfcommForClient(
            Context p0, IRfcommCallback p1, BTDevice p2, int p3, boolean p4);

    public abstract Rfcomm getRfcommForClient(
            Context p0, IRfcommCallback p1, BTDevice p2, java.util.UUID p3, boolean p4);

    public abstract Rfcomm getRfcommForServer(Context p0, IRfcommCallback p1, int p2, boolean p3);

    public abstract Rfcomm getRfcommForServer(
            Context p0, IRfcommCallback p1, String p2, java.util.UUID p3, boolean p4);

    public abstract int getScanMode();

    public abstract Spp getSpp(ISppCallback p0);

    public abstract int getState();

    public boolean isA2dpSinkDevice(BTDevice p0) {
        return false;
    }

    public boolean isBTRecordEnable(int p0) {
        return false;
    }

    public abstract boolean isDiscovering();

    public abstract boolean isEnabled();

    public boolean isMobileDevice(BTDevice p0) {
        return false;
    }

    public abstract boolean removeBond(BTDevice p0);

    public abstract void requestBondedDevices();

    public abstract void setDiscoverableTimeout(int p0);

    public abstract void setName(String p0);

    public abstract void setScanMode(int p0);

    public abstract boolean setScanMode(int p0, int p1);

    public abstract boolean startDiscovery();

    protected BTAdapter(Context context, BTDeviceManager manager) {
        this.mContext = context;
        this.mHandler = new Handler(context.getMainLooper());
        this.mDeviceManager = manager;
    }

    public void registerListener(IBTCallback callback) {
        if (callback != null && !mListeners.contains(callback)) {
            mListeners.add(callback);
        }
    }

    public void unregisterListener(IBTCallback callback) {
        if (callback != null) {
            mListeners.remove(callback);
        }
    }

    public Hfp getHfp(IHfpCallback callback) {
        return mHfp;
    }

    public Hid getHid(IHidCallback callback) {
        return hid;
    }

    public boolean ready() {
        return mIsReady;
    }

    /**
     * Destroys the A2DP profile, as the OEM lifecycle does; HFP and PBAP stay outside this double.
     */
    public void onDestory() {
        if (mA2dp != null) {
            mA2dp.onDestory();
        }
    }

    public void initListener() {}

    public java.util.List<BTDevice> getBondedDevices() {
        return new java.util.ArrayList<>();
    }

    public java.util.List<BTDevice> getScannedDevices() {
        return new java.util.ArrayList<>();
    }
}
