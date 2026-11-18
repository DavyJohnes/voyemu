package com.voyah.baselibrary.player;

import android.support.v4.media.session.PlaybackStateCompat;

import com.voyah.baselibrary.lifecycle.SafeMutableLiveData;

public class MediaManager {
    public static final MediaManager INSTANCE = new MediaManager();
    public SafeMutableLiveData<Integer> musicSource = new SafeMutableLiveData<>();
    public PlaybackStateCompat state;
    public boolean playing, teardown;
    public int play, pause, next, previous;

    public static MediaManager getInstance() {
        return INSTANCE;
    }

    public PlaybackStateCompat getPlaybackState(int s) {
        if (teardown) {
            throw new NullPointerException("teardown");
        }

        return state;
    }

    public boolean isUsbPlaying() {
        return playing;
    }

    public void play(int s, String o) {
        play++;
        playing = true;
    }

    public void pauseHoldFocus(int s, String o) {
        pause++;
        playing = false;
    }

    public void skipToNext(int s, String o) {
        next++;
    }

    public void skipToPrevious(int s, String o) {
        previous++;
    }
}
