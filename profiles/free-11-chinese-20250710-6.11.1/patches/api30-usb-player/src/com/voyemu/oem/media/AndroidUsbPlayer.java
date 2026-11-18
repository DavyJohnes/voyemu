package com.voyemu.oem.media;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.MediaDataSource;
import android.media.MediaPlayer;
import android.net.Uri;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;

import org.ijkplayer.AbstractMediaPlayer;
import org.ijkplayer.MediaInfo;
import org.ijkplayer.misc.IMediaDataSource;
import org.ijkplayer.misc.ITrackInfo;

import java.io.FileDescriptor;
import java.io.IOException;
import java.util.Map;

/** Platform decoding for the pinned OEM USB controller; its queue and session remain OEM-owned. */
public final class AndroidUsbPlayer extends AbstractMediaPlayer {
    private final MediaPlayer player;
    private String source;

    public AndroidUsbPlayer() {
        player = new MediaPlayer();
        player.setOnPreparedListener(value -> notifyOnPrepared());
        player.setOnCompletionListener(value -> notifyOnCompletion());
        player.setOnBufferingUpdateListener((value, percent) -> notifyOnBufferingUpdate(percent));
        player.setOnSeekCompleteListener(value -> notifyOnSeekComplete());
        player.setOnErrorListener((value, what, extra) -> {
            try {
                return notifyOnError(what, extra);
            } catch (IOException error) {
                Log.e("VoyemuUsbPlayer", "OEM error callback failed", error);

                // The OEM event handler skips completion when this callback throws.
                return true;
            }
        });
        player.setOnInfoListener((value, what, extra) -> {
            try {
                return notifyOnInfo(what, extra);
            } catch (IOException error) {
                Log.e("VoyemuUsbPlayer", "OEM info callback failed", error);

                return false;
            }
        });
        player.setOnVideoSizeChangedListener(
                (value, width, height) -> notifyOnVideoSizeChanged(width, height, 1, 1));
    }

    public void setAudioAttributes(AudioAttributes attributes) {
        player.setAudioAttributes(attributes);
    }

    public void setOption(int category, String name, long value) {
        // This is the sole option at the exact OEM USB call site. Platform prepare never
        // auto-starts.
        if (category != 4 || !"start-on-prepared".equals(name) || value != 0) {
            throw new IllegalArgumentException("Unsupported OEM USB player option");
        }
    }

    @Override
    public void setDataSource(Context context, Uri uri) throws IOException {
        player.setDataSource(context, uri);
        source = uri.toString();
    }

    @Override
    public void setDataSource(Context context, Uri uri, Map<String, String> headers)
            throws IOException {
        player.setDataSource(context, uri, headers);
        source = uri.toString();
    }

    @Override
    public void setDataSource(String path) throws IOException {
        player.setDataSource(path);
        source = path;
    }

    @Override
    public void setDataSource(FileDescriptor descriptor) throws IOException {
        player.setDataSource(descriptor);
        source = null;
    }

    @Override
    public void setDataSource(final IMediaDataSource input) {
        player.setDataSource(
                new MediaDataSource() {
                    @Override
                    public int readAt(long position, byte[] buffer, int offset, int size)
                            throws IOException {
                        return input.readAt(position, buffer, offset, size);
                    }

                    @Override
                    public long getSize() throws IOException {
                        return input.getSize();
                    }

                    @Override
                    public void close() throws IOException {
                        input.close();
                    }
                });
        source = null;
    }

    @Override
    public String getDataSource() {
        return source;
    }

    @Override
    public void prepareAsync() {
        player.prepareAsync();
    }

    @Override
    public void start() {
        player.start();
    }

    @Override
    public void pause() {
        player.pause();
    }

    @Override
    public void stop() {
        player.stop();
    }

    @Override
    public void seekTo(long position) {
        if (position < 0 || position > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("USB seek out of range");
        }

        player.seekTo((int) position);
    }

    @Override
    public long getCurrentPosition() {
        return player.getCurrentPosition();
    }

    @Override
    public long getDuration() {
        return player.getDuration();
    }

    @Override
    public boolean isPlaying() {
        return player.isPlaying();
    }

    @Override
    public void reset() {
        player.reset();
        source = null;
    }

    @Override
    public void release() {
        player.release();
        resetListeners();
        source = null;
    }

    @Override
    public void setAudioStreamType(int stream) {
        player.setAudioStreamType(stream);
    }

    @Override
    public int getAudioSessionId() {
        return player.getAudioSessionId();
    }

    @Override
    public void setVolume(float left, float right) {
        player.setVolume(left, right);
    }

    @Override
    public void setLooping(boolean looping) {
        player.setLooping(looping);
    }

    @Override
    public boolean isLooping() {
        return player.isLooping();
    }

    @Override
    public void setDisplay(SurfaceHolder display) {
        player.setDisplay(display);
    }

    @Override
    public void setSurface(Surface surface) {
        player.setSurface(surface);
    }

    @Override
    public void setSurface(Surface surface, View view) {
        player.setSurface(surface);
    }

    @Override
    public void setScreenOnWhilePlaying(boolean enabled) {
        player.setScreenOnWhilePlaying(enabled);
    }

    @Override
    public void setWakeMode(Context context, int mode) {
        player.setWakeMode(context, mode);
    }

    @Override
    public int getVideoWidth() {
        return player.getVideoWidth();
    }

    @Override
    public int getVideoHeight() {
        return player.getVideoHeight();
    }

    @Override
    public int getVideoSarNum() {
        return 1;
    }

    @Override
    public int getVideoSarDen() {
        return 1;
    }

    @Override
    public boolean isPlayable() {
        return true;
    }

    @Override
    public void setKeepInBackground(boolean enabled) {}

    @Override
    public void setLogEnabled(boolean enabled) {}

    @Override
    public MediaInfo getMediaInfo() {
        throw new UnsupportedOperationException(
                "OEM USB controller does not request codec metadata");
    }

    @Override
    public ITrackInfo[] getTrackInfo() {
        throw new UnsupportedOperationException(
                "OEM USB controller does not request track metadata");
    }
}
