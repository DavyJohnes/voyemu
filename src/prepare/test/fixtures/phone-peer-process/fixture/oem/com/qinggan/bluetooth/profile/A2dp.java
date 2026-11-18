package com.qinggan.bluetooth.profile;

/**
 * Test-only original OEM base constructor/listener behavior; compile-only no-op stubs are not
 * runtime proof.
 */
public abstract class A2dp implements IBTProfile {
    protected final android.content.Context mContext;
    protected final com.qinggan.bluetooth.BTDeviceManager mDeviceManager;
    protected final android.os.Handler mHandler;
    protected final java.util.List<IA2dpCallback> mCallbacks =
            new java.util.concurrent.CopyOnWriteArrayList<IA2dpCallback>();

    protected A2dp(android.content.Context context, com.qinggan.bluetooth.BTDeviceManager manager) {
        mContext = context;
        mDeviceManager = manager;
        mHandler = new android.os.Handler(context.getMainLooper());
    }

    public abstract boolean backward();

    public abstract void changeFolderPath(int direction, byte[] path);

    public abstract boolean forward();

    public java.util.List<BTDevice> getConnectedDevices() {
        return new java.util.ArrayList<>();
    }

    public void initListener() {}

    public abstract boolean isPlaying();

    public abstract void mediaBrowsing();

    public abstract void nowPlayingReq();

    public int testDestroyed;

    public void onDestory() {
        testDestroyed++;
    }

    public abstract boolean pause();

    public abstract boolean play();

    public abstract void playMediaItem(byte[] item);

    public void registerListener(final IA2dpCallback callback) {
        if (callback != null && !mCallbacks.contains(callback)) {
            mCallbacks.add(callback);

            if (isProfileReady()) {
                mHandler.post(() -> callback.onReady());
            }
        }
    }

    public abstract boolean startFastForward();

    public abstract boolean startRewind();

    public abstract boolean stop();

    public abstract boolean stopFastForward();

    public abstract boolean stopRewind();

    public void unregisterListener(IA2dpCallback callback) {
        if (callback != null) {
            mCallbacks.remove(callback);
        }
    }

    public abstract boolean volumnDown();

    public abstract boolean volumnUp();
}
