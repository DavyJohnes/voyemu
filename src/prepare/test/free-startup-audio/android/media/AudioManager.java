package android.media;

public class AudioManager {
    public final int[] volume = new int[12], minimum = new int[12];
    public int writes, reads, minima, failRead = -1;
    public boolean ignoreWrites;

    // Exact API30 AudioManager.isPublicStreamType gate; private streams still exist in
    // AudioService.
    public int getStreamMinVolume(int stream) {
        if (stream < 0 || stream > 10 || stream == 6 || stream == 7 || stream == 9) {
            throw new IllegalArgumentException("Invalid stream type " + stream);
        }

        return getStreamMinVolumeInt(stream);
    }

    public int getStreamMinVolumeInt(int stream) {
        minima++;

        return minimum[stream];
    }

    public int getStreamVolume(int stream) {
        reads++;

        return stream == failRead ? 1 : volume[stream];
    }

    public void setStreamVolume(int stream, int value, int flags) {
        writes++;

        if (!ignoreWrites) {
            volume[stream] = value;
        }
    }
}
