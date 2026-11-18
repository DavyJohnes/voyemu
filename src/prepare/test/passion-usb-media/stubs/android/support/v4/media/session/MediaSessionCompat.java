package android.support.v4.media.session;

import android.content.Intent;
import android.os.Handler;

public class MediaSessionCompat {
    public static class Callback {
        public boolean onMediaButtonEvent(Intent intent) {
            return false;
        }

        public void onPlay() {}

        public void onPause() {}

        public void onSkipToNext() {}

        public void onSkipToPrevious() {}
    }

    public Callback callback;
    public Handler handler;
    public boolean active, released;
    public PlaybackStateCompat state;

    public void setCallback(Callback c, Handler h) {
        callback = c;
        handler = h;
    }

    public void setActive(boolean a) {
        active = a;
    }

    public void setPlaybackState(PlaybackStateCompat s) {
        state = s;
    }

    public void release() {
        released = true;
    }
}
