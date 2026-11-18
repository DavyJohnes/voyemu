package com.qinggan.audiopolicy;

public interface IAudioPolicyInfoCallback extends android.os.IInterface {
    void onAudioPolicyInfoChange(AudioPolicyInfo p);
}
