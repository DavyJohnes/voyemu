package android.media;

public class AudioManager {
    public static final int STREAM_MUSIC = 3,
            STREAM_VOICE_CALL = 0,
            STREAM_ALARM = 4,
            STREAM_RING = 2,
            STREAM_NOTIFICATION = 5;
    public int[] values = new int[12];

    public int getStreamMinVolume(int s) {
        return 0;
    }

    public int getStreamMaxVolume(int s) {
        return 10;
    }

    public int getStreamVolume(int s) {
        return values[s];
    }

    public void setStreamVolume(int s, int v, int flags) {
        values[s] = v;
    }
}
