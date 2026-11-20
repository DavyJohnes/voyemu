package com.qinggan.bluetooth.profile;

public abstract class A2dp implements IBTProfile {
    protected A2dp(android.content.Context p0, com.qinggan.bluetooth.BTDeviceManager p1) {}

    public abstract boolean backward();

    public abstract void changeFolderPath(int p0, byte[] p1);

    public abstract boolean forward();

    public abstract boolean isPlaying();

    public abstract void mediaBrowsing();

    public abstract void nowPlayingReq();

    public abstract boolean pause();

    public abstract boolean play();

    public abstract void playMediaItem(byte[] p0);

    public void registerListener(IA2dpCallback p0) {}

    public abstract boolean startFastForward();

    public abstract boolean startRewind();

    public abstract boolean stop();

    public abstract boolean stopFastForward();

    public abstract boolean stopRewind();

    public abstract boolean volumnDown();

    public abstract boolean volumnUp();
}
