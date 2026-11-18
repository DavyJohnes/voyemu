package org.ijkplayer;

public abstract class AbstractMediaPlayer implements IMediaPlayer {
    public AbstractMediaPlayer() {}

    protected final void notifyOnBufferingUpdate(int p0) {}

    protected final void notifyOnCompletion() {}

    protected final boolean notifyOnError(int p0, int p1) throws java.io.IOException {
        return false;
    }

    protected final boolean notifyOnInfo(int p0, int p1) throws java.io.IOException {
        return false;
    }

    protected final void notifyOnPrepared() {}

    protected final void notifyOnSeekComplete() {}

    protected final void notifyOnVideoSizeChanged(int p0, int p1, int p2, int p3) {}

    public void resetListeners() {}

    @Override
    public void setDataSource(org.ijkplayer.misc.IMediaDataSource p0) {}
}
