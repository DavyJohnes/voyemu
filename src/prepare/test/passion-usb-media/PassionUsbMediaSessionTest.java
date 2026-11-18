import android.content.Intent;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import android.view.KeyEvent;

import com.voyah.baselibrary.player.MediaManager;
import com.voyemu.media.PassionUsbMediaSession;

public class PassionUsbMediaSessionTest {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        MediaManager m = MediaManager.INSTANCE;
        Object first = new Object(), second = new Object();
        MediaSessionCompat a = new MediaSessionCompat();

        PassionUsbMediaSession.install(first, a);
        checkPlaybackGate(m, a);
        checkKeyPhases(m, a);
        checkReinstall(m, a, first, second);
        System.out.println("USB media real-state gate, key phases and lifecycle PASS");
    }

    static void checkPlaybackGate(MediaManager m, MediaSessionCompat a) {
        check(!a.active && a.state == null);
        m.musicSource.value = 1;
        m.state = new PlaybackStateCompat(3);
        a.handler.pending.run();
        check(!a.active && a.state == null);
        m.musicSource.value = 0;
        m.state = null;
        a.handler.pending.run();
        check(!a.active);
        m.teardown = true;
        a.handler.pending.run();
        check(!a.active && android.util.Log.warnings == 1);
        m.teardown = false;
        m.state = new PlaybackStateCompat(3);
        m.playing = true;
        a.handler.pending.run();
        check(a.active && a.state.state == 3 && a.state.actions == 566);
    }

    static void checkKeyPhases(MediaManager m, MediaSessionCompat a) {
        check(!a.callback.onMediaButtonEvent(null));
        check(!a.callback.onMediaButtonEvent(new Intent(null)));
        check(!a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 24, 0))));
        check(!a.callback.onMediaButtonEvent(new Intent(new KeyEvent(2, 87, 0))));
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 87, 0))) && m.next == 0);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 87, 0))));
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 88, 0))));
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 88, 0))) && m.previous == 0);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 87, 32))) && m.next == 0);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 87, 0))) && m.next == 0);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 87, 0))) && m.next == 1);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 88, 0))));
        check(
                a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 88, 32)))
                        && m.previous == 0);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 85, 0))));
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 85, 0))) && m.pause == 1);
        m.state = new PlaybackStateCompat(2);
        a.handler.pending.run();
        check(a.active && a.state.state == 2);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 85, 0))));
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 85, 0))) && m.play == 1);
        a.callback.onSkipToNext();
        a.callback.onSkipToPrevious();
        a.callback.onPause();
        a.callback.onPlay();
        check(m.next == 2 && m.previous == 1 && m.pause == 2 && m.play == 2);
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(0, 87, 0))));
        m.musicSource.value = 1;
        check(a.callback.onMediaButtonEvent(new Intent(new KeyEvent(1, 87, 0))) && m.next == 2);
        a.handler.pending.run();
        check(!a.active && a.state == null);
    }

    static void checkReinstall(MediaManager m, MediaSessionCompat a, Object first, Object second) {
        MediaSessionCompat.Callback stale = a.callback;
        MediaSessionCompat b = new MediaSessionCompat();

        PassionUsbMediaSession.install(second, b);
        check(a.released && !a.active && a.callback == null && a.handler.pending == null);
        m.musicSource.value = 0;
        stale.onSkipToNext();
        check(!stale.onMediaButtonEvent(new Intent(new KeyEvent(0, 87, 0))) && m.next == 2);
        PassionUsbMediaSession.dispose(first);
        check(!b.released);
        PassionUsbMediaSession.dispose(second);
        check(b.released && !b.active && b.callback == null && b.handler.pending == null);
    }
}
