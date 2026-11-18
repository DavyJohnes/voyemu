package com.voyah.baselibrary.player;

import android.support.v4.media.session.PlaybackStateCompat;

import com.voyah.baselibrary.lifecycle.SafeMutableLiveData;

public class MediaManager {
    public SafeMutableLiveData<Integer> musicSource;

    public static MediaManager getInstance() {
        return null;
    }

    public PlaybackStateCompat getPlaybackState(int source) {
        return null;
    }

    public boolean isUsbPlaying() {
        return false;
    }

    public void play(int source, String origin) {}

    public void pauseHoldFocus(int source, String origin) {}

    public void skipToNext(int source, String origin) {}

    public void skipToPrevious(int source, String origin) {}
}
