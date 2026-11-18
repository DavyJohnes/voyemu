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

    public void setCallback(Callback callback, Handler handler) {}

    public void setActive(boolean active) {}

    public void setPlaybackState(PlaybackStateCompat state) {}

    public void release() {}
}
