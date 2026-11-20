package com.qinggan.bluetooth.profile;

public abstract class Hfp implements IBTProfile {
    protected android.os.Handler mHandler;

    protected Hfp(android.content.Context p0, com.qinggan.bluetooth.BTDeviceManager p1) {}

    public abstract void answerCall();

    public abstract void closeSse();

    public abstract void connectAudio();

    public abstract void dial(String p0);

    public void dial(String p0, String p1) {}

    public abstract void disConnectAudio();

    public int getBatteryLevel() {
        return 0;
    }

    public abstract int getCallState();

    public abstract BTDevice getConnectedDevice();

    public java.util.List<BTDevice> getConnectedDevices() {
        return null;
    }

    public java.util.List<BluetoothCall> getCurrentBluetoothCalls() {
        return null;
    }

    public abstract java.util.List<java.util.Map<String, Object>> getCurrentCalls();

    public int getNetworkStatus() {
        return 0;
    }

    public abstract String getPhoneNumber();

    public String getPhoneNumber(String p0) {
        return null;
    }

    public int getSignalStrength() {
        return 0;
    }

    public abstract void hangup();

    public void hangup(int p0) {}

    public abstract void hangupCallAndActiveOtherCall();

    public abstract void holdCallAndActiveOtherCall();

    public abstract boolean isAudioOn();

    public abstract boolean isBandRingtone();

    public boolean isHfpServiceConnected() {
        return false;
    }

    public abstract void openSse();

    public void registerListener(IHfpCallback p0) {}

    public abstract void resumeCallAndRejectOtherCall();

    public abstract boolean scoAudioSwitch(BTDevice p0, boolean p1);

    public abstract void startScoUsingVirtualVoiceCall(BTDevice p0);

    public abstract boolean startVoiceRecognition(BTDevice p0);

    public abstract void stopScoUsingVirtualVoiceCall(BTDevice p0);

    public abstract boolean stopVoiceRecognition(BTDevice p0);

    public abstract void transferDTMF(char p0);

    public void unregisterListener(IHfpCallback p0) {}
}
