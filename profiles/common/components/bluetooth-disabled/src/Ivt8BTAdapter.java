package com.qinggan.bluetooth.internal.ivt8;

import android.content.Context;
import android.util.Log;

import com.qinggan.bluetooth.BTConst;
import com.qinggan.bluetooth.BTDeviceManager;
import com.qinggan.bluetooth.profile.A2dp;
import com.qinggan.bluetooth.profile.BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.IA2dpCallback;
import com.qinggan.bluetooth.profile.IBTCallback;
import com.qinggan.bluetooth.profile.IPbapCallback;
import com.qinggan.bluetooth.profile.IRfcommCallback;
import com.qinggan.bluetooth.profile.ISppCallback;
import com.qinggan.bluetooth.profile.Pbap;
import com.qinggan.bluetooth.profile.Rfcomm;
import com.qinggan.bluetooth.profile.Spp;

import java.util.UUID;

/**
 * No-op stub Ivt8BTAdapter for emulator environment. Reports unavailable hardware and completes
 * power requests in the OEM off state. Replaces the real Ivt8BTAdapter which requires physical
 * Bluetooth hardware.
 */
public final class Ivt8BTAdapter extends BTAdapter {

    private static final String TAG = "Ivt8BTAdapter";
    private static Ivt8BTAdapter sInstance;

    private Ivt8BTAdapter(Context context, BTDeviceManager manager) {
        super(context, manager);
        Log.d(TAG, "Ivt8BTAdapter stub created (emulator no-op)");
        mIsReady = true;
    }

    public static synchronized Ivt8BTAdapter getInstance(Context context, BTDeviceManager manager) {
        if (sInstance == null) {
            sInstance = new Ivt8BTAdapter(context, manager);
        }

        return sInstance;
    }

    @Override
    public boolean cancelDiscovery() {
        return false;
    }

    @Override
    public boolean connect(BTDevice device) {
        return false;
    }

    @Override
    public boolean createBond(BTDevice device) {
        return false;
    }

    @Override
    public boolean disable() {
        mHandler.post(() -> {
            for (IBTCallback listener : mListeners) {
                listener.onBTStateChanged(BTConst.STATE_OFF, BTConst.STATE_OFF);
            }
        });

        return false;
    }

    @Override
    public boolean disconnect(BTDevice device) {
        return false;
    }

    @Override
    public boolean enable() {
        mHandler.post(() -> {
            for (IBTCallback listener : mListeners) {
                listener.onBTStateChanged(BTConst.STATE_OFF, BTConst.STATE_OFF);
            }
        });

        return false;
    }

    @Override
    public boolean enableAutoBond() {
        return false;
    }

    @Override
    public A2dp getA2dp(IA2dpCallback callback) {
        return null;
    }

    @Override
    public String getAddress() {
        return "00:00:00:00:00:00";
    }

    @Override
    public int getBondState(BTDevice device) {
        return BTConst.BOND_NONE;
    }

    @Override
    public BTDevice getConnectedDevice() {
        return null;
    }

    @Override
    public int getConnectionState() {
        return BTConst.CONNECTION_STATE_DISCONNECTED;
    }

    @Override
    public int getConnectionState(BTDevice device) {
        return BTConst.CONNECTION_STATE_DISCONNECTED;
    }

    @Override
    public String getName() {
        return "Voyemu-Emulator";
    }

    @Override
    public Pbap getPbap(IPbapCallback callback) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForClient(
            Context context, IRfcommCallback callback, BTDevice device, int value, boolean flag) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForClient(
            Context context, IRfcommCallback callback, BTDevice device, UUID uuid, boolean flag) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForServer(
            Context context, IRfcommCallback callback, int value, boolean flag) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForServer(
            Context context, IRfcommCallback callback, String text, UUID uuid, boolean flag) {
        return null;
    }

    @Override
    public int getScanMode() {
        return BTConst.SCAN_MODE_NONE;
    }

    @Override
    public Spp getSpp(ISppCallback callback) {
        return null;
    }

    @Override
    public int getState() {
        return BTConst.STATE_OFF;
    }

    @Override
    public boolean isDiscovering() {
        return false;
    }

    @Override
    public boolean isEnabled() {
        return false;
    }

    @Override
    public boolean removeBond(BTDevice device) {
        return false;
    }

    @Override
    public void requestBondedDevices() {}

    @Override
    public void setDiscoverableTimeout(int value) {}

    @Override
    public void setName(String text) {}

    @Override
    public void setScanMode(int value) {}

    @Override
    public boolean setScanMode(int value, int value2) {
        return false;
    }

    @Override
    public boolean startDiscovery() {
        return false;
    }
}
