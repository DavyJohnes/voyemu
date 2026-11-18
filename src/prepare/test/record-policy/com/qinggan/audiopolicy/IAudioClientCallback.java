package com.qinggan.audiopolicy;

public interface IAudioClientCallback extends android.os.IInterface {
    void onAudioClientChange(AudioClient c);
}
