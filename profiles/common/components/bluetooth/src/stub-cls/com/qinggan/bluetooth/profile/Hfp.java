package com.qinggan.bluetooth.profile;

public abstract class Hfp implements IBTProfile {
    protected Hfp(android.content.Context p0, com.qinggan.bluetooth.BTDeviceManager p1) {}

    public abstract void answerCall();

    public abstract void closeSse();

    public abstract void connectAudio();

    public abstract void dial(java.lang.String p0);

    public abstract void disConnectAudio();

    public abstract int getCallState();

    public abstract com.qinggan.bluetooth.profile.BTDevice getConnectedDevice();

    public abstract java.util.List getCurrentCalls();

    public abstract java.lang.String getPhoneNumber();

    public abstract void hangup();

    public abstract void hangupCallAndActiveOtherCall();

    public abstract void holdCallAndActiveOtherCall();

    public abstract boolean isAudioOn();

    public abstract boolean isBandRingtone();

    public abstract void openSse();

    public abstract void resumeCallAndRejectOtherCall();

    public abstract boolean scoAudioSwitch(com.qinggan.bluetooth.profile.BTDevice p0, boolean p1);

    public abstract void startScoUsingVirtualVoiceCall(com.qinggan.bluetooth.profile.BTDevice p0);

    public abstract boolean startVoiceRecognition(com.qinggan.bluetooth.profile.BTDevice p0);

    public abstract void stopScoUsingVirtualVoiceCall(com.qinggan.bluetooth.profile.BTDevice p0);

    public abstract boolean stopVoiceRecognition(com.qinggan.bluetooth.profile.BTDevice p0);

    public abstract void transferDTMF(char p0);

    public void registerListener(IHfpCallback p0) {}

    public void unregisterListener(IHfpCallback p0) {}

    public java.util.List<BluetoothCall> getCurrentBluetoothCalls() {
        return null;
    }

    public java.util.List<BTDevice> getConnectedDevices() {
        return null;
    }

    public boolean isHfpServiceConnected() {
        return false;
    }

    public int getNetworkStatus() {
        return 0;
    }

    public int getSignalStrength() {
        return 0;
    }

    public int getBatteryLevel() {
        return 0;
    }

    public java.lang.String getPhoneNumber(java.lang.String p0) {
        return null;
    }

    public void dial(java.lang.String p0, java.lang.String p1) {}

    public void hangup(int p0) {}
}
