package com.qinggan.audiopolicy;

public interface IAudioPolicyInfoCallback extends android.os.IInterface {
    void onAudioPolicyInfoChange(AudioPolicyInfo p0) throws android.os.RemoteException;

    void onMuteStateChange(boolean p0, int p1) throws android.os.RemoteException;

    void onStreamVolumeChange(int p0, int p1, int p2) throws android.os.RemoteException;
}
