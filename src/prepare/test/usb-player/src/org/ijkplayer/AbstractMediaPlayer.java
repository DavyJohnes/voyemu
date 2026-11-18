package org.ijkplayer;

import android.content.Context;
import android.net.Uri;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;

import org.ijkplayer.misc.IMediaDataSource;
import org.ijkplayer.misc.ITrackInfo;

import java.io.FileDescriptor;
import java.io.IOException;
import java.util.Map;

public abstract class AbstractMediaPlayer {
    public interface VoidListener {
        void call(AbstractMediaPlayer p);
    }

    public interface PairListener {
        boolean call(AbstractMediaPlayer p, int a, int b) throws IOException;
    }

    public VoidListener prepared, completed, seek;
    public PairListener error, info;
    public int buffering, width, height, sarN, sarD;

    public void resetListeners() {
        prepared = null;
        completed = null;
        seek = null;
        error = null;
        info = null;
    }

    protected final void notifyOnPrepared() {
        if (prepared != null) {
            prepared.call(this);
        }
    }

    protected final void notifyOnCompletion() {
        if (completed != null) {
            completed.call(this);
        }
    }

    protected final void notifyOnSeekComplete() {
        if (seek != null) {
            seek.call(this);
        }
    }

    protected final void notifyOnBufferingUpdate(int n) {
        buffering = n;
    }

    protected final void notifyOnVideoSizeChanged(int w, int h, int n, int d) {
        width = w;
        height = h;
        sarN = n;
        sarD = d;
    }

    protected final boolean notifyOnError(int a, int b) throws IOException {
        return error != null && error.call(this, a, b);
    }

    protected final boolean notifyOnInfo(int a, int b) throws IOException {
        return info != null && info.call(this, a, b);
    }

    public abstract void setDataSource(Context context, Uri uri) throws IOException;

    public abstract void setDataSource(Context context, Uri uri, Map<String, String> headers)
            throws IOException;

    public abstract void setDataSource(String path) throws IOException;

    public abstract void setDataSource(FileDescriptor descriptor) throws IOException;

    public abstract void setDataSource(IMediaDataSource input);

    public abstract String getDataSource();

    public abstract void prepareAsync();

    public abstract void start();

    public abstract void pause();

    public abstract void stop();

    public abstract void seekTo(long position);

    public abstract long getCurrentPosition();

    public abstract long getDuration();

    public abstract boolean isPlaying();

    public abstract void reset();

    public abstract void release();

    public abstract void setAudioStreamType(int stream);

    public abstract int getAudioSessionId();

    public abstract void setVolume(float left, float right);

    public abstract void setLooping(boolean looping);

    public abstract boolean isLooping();

    public abstract void setDisplay(SurfaceHolder display);

    public abstract void setSurface(Surface surface);

    public abstract void setSurface(Surface surface, View view);

    public abstract void setScreenOnWhilePlaying(boolean enabled);

    public abstract void setWakeMode(Context context, int mode);

    public abstract int getVideoWidth();

    public abstract int getVideoHeight();

    public abstract int getVideoSarNum();

    public abstract int getVideoSarDen();

    public abstract boolean isPlayable();

    public abstract void setKeepInBackground(boolean enabled);

    public abstract void setLogEnabled(boolean enabled);

    public abstract MediaInfo getMediaInfo();

    public abstract ITrackInfo[] getTrackInfo();
}
