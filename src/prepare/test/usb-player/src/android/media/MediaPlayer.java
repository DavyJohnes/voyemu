package android.media;

import android.content.Context;
import android.net.Uri;
import android.view.Surface;
import android.view.SurfaceHolder;

import java.io.FileDescriptor;
import java.io.IOException;
import java.util.Map;
import java.util.Set;

public class MediaPlayer {
    public static MediaPlayer last;
    public String state = "idle";
    public int starts, releases, resets, position, duration = 123456, seekCalls;
    public boolean looping;
    public MediaDataSource data;

    public interface One {
        void call(MediaPlayer p);
    }

    public interface Two {
        void call(MediaPlayer p, int n);
    }

    public interface Three {
        boolean call(MediaPlayer p, int a, int b);
    }

    public interface Video {
        void call(MediaPlayer p, int w, int h);
    }

    public One prepared, completion, seek;
    public Two buffering;
    public Three error, info;
    public Video video;

    public MediaPlayer() {
        last = this;
    }

    public void setOnPreparedListener(One l) {
        prepared = l;
    }

    public void setOnCompletionListener(One l) {
        completion = l;
    }

    public void setOnSeekCompleteListener(One l) {
        seek = l;
    }

    public void setOnBufferingUpdateListener(Two l) {
        buffering = l;
    }

    public void setOnErrorListener(Three l) {
        error = l;
    }

    public void setOnInfoListener(Three l) {
        info = l;
    }

    public void setOnVideoSizeChangedListener(Video l) {
        video = l;
    }

    void alive() {
        if (state.equals("released")) {
            throw new IllegalStateException("released");
        }
    }

    void ready() {
        alive();

        if (!Set.of("prepared", "started", "paused", "completed").contains(state)) {
            throw new IllegalStateException(state);
        }
    }

    void source(String s) throws IOException {
        alive();

        if (!state.equals("idle")) {
            throw new IllegalStateException(state);
        }

        if (s.equals("bad")) {
            throw new IOException("source-failed");
        }

        state = "initialized";
    }

    public void setDataSource(String s) throws IOException {
        source(s);
    }

    public void setDataSource(Context c, Uri u) throws IOException {
        source(u.toString());
    }

    public void setDataSource(Context c, Uri u, Map<String, String> h) throws IOException {
        source(u.toString());
    }

    public void setDataSource(FileDescriptor f) throws IOException {
        source("fd");
    }

    public void setDataSource(MediaDataSource d) {
        try {
            source("data");
            data = d;
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    public void prepareAsync() {
        alive();

        if (!state.equals("initialized")) {
            throw new IllegalStateException(state);
        }

        state = "preparing";
    }

    public void firePrepared() {
        if (!state.equals("preparing")) {
            throw new IllegalStateException(state);
        }

        state = "prepared";
        prepared.call(this);
    }

    public void start() {
        ready();
        starts++;
        state = "started";
    }

    public void pause() {
        ready();
        state = "paused";
    }

    public void stop() {
        ready();
        state = "stopped";
    }

    public boolean isPlaying() {
        alive();

        return state.equals("started");
    }

    public int getCurrentPosition() {
        ready();

        return position;
    }

    public int getDuration() {
        ready();

        return duration;
    }

    public void seekTo(int p) {
        ready();
        position = p;
        seekCalls++;
    }

    public void fireSeek() {
        seek.call(this);
    }

    public void fireComplete() {
        state = "completed";
        completion.call(this);
    }

    public boolean fireError(int a, int b) {
        state = "error";
        boolean handled = error.call(this, a, b);

        if (!handled) {
            completion.call(this);
        }

        return handled;
    }

    public void reset() {
        alive();
        resets++;
        state = "idle";
        position = 0;
    }

    public void release() {
        releases++;
        state = "released";
    }

    public void setAudioAttributes(AudioAttributes a) {}

    public void setAudioStreamType(int a) {}

    public int getAudioSessionId() {
        return 42;
    }

    public void setVolume(float a, float b) {}

    public void setLooping(boolean l) {
        looping = l;
    }

    public boolean isLooping() {
        return looping;
    }

    public void setDisplay(SurfaceHolder h) {}

    public void setSurface(Surface s) {}

    public void setScreenOnWhilePlaying(boolean e) {}

    public void setWakeMode(Context c, int m) {}

    public int getVideoWidth() {
        return 320;
    }

    public int getVideoHeight() {
        return 240;
    }
}
