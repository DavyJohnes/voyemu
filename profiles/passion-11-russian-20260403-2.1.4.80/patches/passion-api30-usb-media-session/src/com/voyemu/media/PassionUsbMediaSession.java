package com.voyemu.media;

import android.content.Intent;
import android.os.Handler;
import android.os.Looper;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import android.util.Log;
import android.view.KeyEvent;

import com.voyah.baselibrary.player.MediaManager;

/** Binds the OEM session to its actual USB player; no Bluetooth or cloud emulation. */
public final class PassionUsbMediaSession {
    private static final long ACTIONS = 4L | 2L | 32L | 16L | 512L;
    private static Bridge current;

    private PassionUsbMediaSession() {}

    public static synchronized void install(Object owner, MediaSessionCompat session) {
        if (current != null) {
            current.close();
        }

        current = new Bridge(owner, session);
        current.start();
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    public static synchronized void dispose(Object owner) {
        if (current != null && current.owner == owner) {
            current.close();
            current = null;
        }
    }

    private static final class Bridge extends MediaSessionCompat.Callback implements Runnable {
        final Object owner;
        final MediaSessionCompat session;
        final Handler handler = new Handler(Looper.getMainLooper());
        volatile boolean closed;
        boolean active;
        int held;

        Bridge(Object owner, MediaSessionCompat session) {
            this.owner = owner;
            this.session = session;
        }

        void start() {
            session.setCallback(this, handler);
            run();
        }

        PlaybackStateCompat realState() {
            MediaManager m = MediaManager.getInstance();
            Integer source = m.musicSource.getValue();

            if (source == null || source.intValue() != 0) {
                return null;
            }

            try {
                return m.getPlaybackState(1);
            } catch (NullPointerException teardown) {
                Log.w(
                        "PassionUsbMediaSession",
                        "USB state unavailable during controller teardown",
                        teardown);

                return null;
            }
        }

        @Override
        public void run() {
            if (closed) {
                return;
            }

            PlaybackStateCompat state = realState();
            boolean ready = state != null;

            if (ready) {
                session.setPlaybackState(
                        new PlaybackStateCompat.Builder(state).setActions(ACTIONS).build());
            } else {
                held = 0;
                session.setPlaybackState(null);
            }

            if (active != ready) {
                session.setActive(ready);
                active = ready;
            }

            handler.postDelayed(this, 250);
        }

        @Override
        public void onPlay() {
            if (!closed && realState() != null) {
                MediaManager m = MediaManager.getInstance();

                if (!m.isUsbPlaying()) {
                    m.play(1, "1");
                }
            }
        }

        @Override
        public void onPause() {
            if (!closed && realState() != null) {
                MediaManager m = MediaManager.getInstance();

                if (m.isUsbPlaying()) {
                    m.pauseHoldFocus(1, "1");
                }
            }
        }

        @Override
        public void onSkipToNext() {
            if (!closed && realState() != null) {
                MediaManager.getInstance().skipToNext(1, "1");
            }
        }

        @Override
        public void onSkipToPrevious() {
            if (!closed && realState() != null) {
                MediaManager.getInstance().skipToPrevious(1, "1");
            }
        }

        @Override
        public boolean onMediaButtonEvent(Intent intent) {
            if (closed) {
                return false;
            }

            KeyEvent event =
                    intent == null
                            ? null
                            : (KeyEvent) intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);

            if (event == null) {
                return false;
            }

            int key = event.getKeyCode();

            if (key != 85 && key != 87 && key != 88) {
                return false;
            }

            return handleKeyEvent(event, key);
        }

        private boolean handleKeyEvent(KeyEvent event, int key) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (realState() == null) {
                    return false;
                }

                if (held == 0) {
                    held = key;
                }

                return true;
            }

            if (event.getAction() != KeyEvent.ACTION_UP) {
                return false;
            }

            if (held != key) {
                return true;
            }

            held = 0;

            if ((event.getFlags() & KeyEvent.FLAG_CANCELED) != 0 || realState() == null) {
                return true;
            }

            dispatchReleasedKey(key);

            return true;
        }

        private void dispatchReleasedKey(int key) {
            if (key == 85) {
                if (MediaManager.getInstance().isUsbPlaying()) {
                    onPause();
                } else {
                    onPlay();
                }
            } else if (key == 87) {
                onSkipToNext();
            } else {
                onSkipToPrevious();
            }
        }

        void close() {
            if (closed) {
                return;
            }

            closed = true;
            held = 0;
            handler.removeCallbacks(this);
            session.setActive(false);
            session.setCallback(null, handler);
            session.release();
            active = false;
        }
    }
}
