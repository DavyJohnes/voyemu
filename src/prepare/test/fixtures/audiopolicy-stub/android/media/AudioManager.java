package android.media;

public class AudioManager {
    public static final int STREAM_MUSIC = 3;
    private int music;

    public int getStreamMaxVolume(int stream) {
        return 15;
    }

    public void setStreamVolume(int stream, int index, int flags) {
        if (stream == STREAM_MUSIC) {
            music = index;
        }
    }

    public int getStreamVolume(int stream) {
        return stream == STREAM_MUSIC ? music : 0;
    }
}
