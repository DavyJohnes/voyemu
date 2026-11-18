package android.media;

import java.util.List;

public class AudioManager {
    public static final int STREAM_MUSIC = 3, ADJUST_MUTE = -100, ADJUST_UNMUTE = 100;

    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int c);
    }

    public int requestAudioFocus(AudioFocusRequest r) {
        return 0;
    }

    public int abandonAudioFocusRequest(AudioFocusRequest r) {
        return 0;
    }

    public int getStreamVolume(int s) {
        return 0;
    }

    public int getStreamMaxVolume(int s) {
        return 0;
    }

    public void setStreamVolume(int s, int v, int f) {}

    public void adjustStreamVolume(int s, int v, int f) {}

    public void adjustVolume(int v, int f) {}

    public boolean isStreamMute(int s) {
        return false;
    }

    public boolean isMicrophoneMute() {
        return false;
    }

    public boolean isMusicActive() {
        return false;
    }

    public List<Object> getActiveRecordingConfigurations() {
        return List.of();
    }
}
