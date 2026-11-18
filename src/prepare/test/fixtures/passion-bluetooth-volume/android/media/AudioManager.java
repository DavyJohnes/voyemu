package android.media;

public class AudioManager {
    public int count = 12, writes, lastStream = -1, lastVolume = -1;
    public boolean unrelated;

    private void valid(int stream) {
        if (unrelated && stream == 12) {
            throw new IllegalArgumentException("unrelated failure");
        }

        if (stream < 0 || stream >= count) {
            throw new IllegalArgumentException("Bad stream type " + stream);
        }
    }

    public int getStreamMaxVolume(int s) {
        valid(s);

        return 21;
    }

    public int getStreamMinVolume(int s) {
        valid(s);

        return 2;
    }

    public int getStreamVolume(int s) {
        valid(s);

        return 7;
    }

    public void setStreamVolume(int s, int v, int flags) {
        valid(s);
        writes++;
        lastStream = s;
        lastVolume = v;
    }
}
