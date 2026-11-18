package android.media;

import android.view.KeyEvent;

public class AudioManager {
    public static final int STREAM_MUSIC = 3, ADJUST_RAISE = 1, ADJUST_LOWER = -1;
    public int calls, stream, direction;
    public int[] levels = new int[12];
    public KeyEvent event;

    public void adjustStreamVolume(int s, int d, int f) {
        calls++;
        stream = s;
        direction = d;
    }

    public void dispatchMediaKeyEvent(KeyEvent e) {
        calls++;
        event = e;
    }

    public void setStreamVolume(int s, int v, int flags) {
        levels[s] = v;
    }

    public int getStreamVolume(int s) {
        return levels[s];
    }
}
