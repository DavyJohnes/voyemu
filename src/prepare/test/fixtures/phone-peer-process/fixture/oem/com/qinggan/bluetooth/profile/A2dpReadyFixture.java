package com.qinggan.bluetooth.profile;

/** Positive control proves the test base actually emits readiness and deduplicates listeners. */
public final class A2dpReadyFixture extends A2dp {
    public A2dpReadyFixture(android.content.Context context) {
        super(context, null);
    }

    public boolean isProfileReady() {
        return true;
    }

    public boolean isConnected() {
        return false;
    }

    public boolean connect(BTDevice d) {
        return false;
    }

    public boolean connect(String s) {
        return false;
    }

    public boolean disconnect(BTDevice d) {
        return false;
    }

    public boolean disconnect(String s) {
        return false;
    }

    public int getConnectionState() {
        return 19;
    }

    public int getConnectionState(BTDevice d) {
        return 19;
    }

    public boolean backward() {
        return false;
    }

    public void changeFolderPath(int d, byte[] p) {}

    public boolean forward() {
        return false;
    }

    public boolean isPlaying() {
        return false;
    }

    public void mediaBrowsing() {}

    public void nowPlayingReq() {}

    public boolean pause() {
        return false;
    }

    public boolean play() {
        return false;
    }

    public void playMediaItem(byte[] b) {}

    public boolean startFastForward() {
        return false;
    }

    public boolean startRewind() {
        return false;
    }

    public boolean stop() {
        return false;
    }

    public boolean stopFastForward() {
        return false;
    }

    public boolean stopRewind() {
        return false;
    }

    public boolean volumnDown() {
        return false;
    }

    public boolean volumnUp() {
        return false;
    }
}
