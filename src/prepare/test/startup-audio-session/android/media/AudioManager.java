package android.media;

public class AudioManager {
    public final int[] values = new int[12];
    public int writes;
    public int reject = -1;

    public void setStreamVolume(int stream, int value, int flags) {
        writes++;

        if (stream != reject) {
            values[stream] = value;
        }
    }

    public int getStreamVolume(int stream) {
        return values[stream];
    }
}
