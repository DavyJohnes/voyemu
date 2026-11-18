package android.media;

public class AudioManager {
    public final int[] values = new int[12];
    public int writes;
    public boolean phoneGranted = true, accessibilityGranted = true;
    public int reject = -1;

    public void setStreamVolume(int stream, int value, int flags) {
        writes++;

        // Exact API28 AudioService silently refuses these writes without signature grants.
        if ((stream == 0 && value == 0 && !phoneGranted)
                || (stream == 10 && !accessibilityGranted)) {
            return;
        }

        if (stream != reject) {
            values[stream] = value;
        }
    }

    public int getStreamVolume(int stream) {
        return values[stream];
    }
}
