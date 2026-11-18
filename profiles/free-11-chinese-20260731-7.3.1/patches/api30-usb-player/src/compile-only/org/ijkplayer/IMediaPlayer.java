package org.ijkplayer;

public interface IMediaPlayer {
    int getAudioSessionId();

    long getCurrentPosition();

    String getDataSource();

    long getDuration();

    MediaInfo getMediaInfo();

    org.ijkplayer.misc.ITrackInfo[] getTrackInfo();

    int getVideoHeight();

    int getVideoSarDen();

    int getVideoSarNum();

    int getVideoWidth();

    boolean isLooping();

    boolean isPlayable();

    boolean isPlaying();

    void pause() throws IllegalStateException;

    void prepareAsync() throws IllegalStateException;

    void release();

    void reset();

    void seekTo(long p0) throws IllegalStateException;

    void setAudioStreamType(int p0);

    void setDataSource(android.content.Context p0, android.net.Uri p1)
            throws java.io.IOException,
                    IllegalArgumentException,
                    SecurityException,
                    IllegalStateException;

    void setDataSource(
            android.content.Context p0, android.net.Uri p1, java.util.Map<String, String> p2)
            throws java.io.IOException,
                    IllegalArgumentException,
                    SecurityException,
                    IllegalStateException;

    void setDataSource(java.io.FileDescriptor p0)
            throws java.io.IOException, IllegalArgumentException, IllegalStateException;

    void setDataSource(String p0)
            throws java.io.IOException,
                    IllegalArgumentException,
                    SecurityException,
                    IllegalStateException;

    void setDataSource(org.ijkplayer.misc.IMediaDataSource p0);

    void setDisplay(android.view.SurfaceHolder p0);

    void setKeepInBackground(boolean p0);

    void setLogEnabled(boolean p0);

    void setLooping(boolean p0);

    void setScreenOnWhilePlaying(boolean p0);

    void setSurface(android.view.Surface p0);

    void setSurface(android.view.Surface p0, android.view.View p1);

    void setVolume(float p0, float p1);

    void setWakeMode(android.content.Context p0, int p1);

    void start() throws IllegalStateException;

    void stop() throws IllegalStateException;
}
