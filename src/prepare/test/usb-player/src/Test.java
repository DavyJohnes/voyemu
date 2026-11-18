import android.media.MediaPlayer;

import com.voyemu.oem.media.AndroidUsbPlayer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class Test {
    static int assertions;

    static void check(boolean x, String m) {
        assertions++;

        if (!x) {
            throw new AssertionError(m);
        }
    }

    interface Action {
        void run() throws Exception;
    }

    static void throwsType(Class<?> type, Action a) throws Exception {
        try {
            a.run();
            throw new AssertionError("missing " + type);
        } catch (Exception e) {
            check(type.isInstance(e), "wrong exception " + e);
        }
    }

    public static void main(String[] args) throws Exception {
        AndroidUsbPlayer p = new AndroidUsbPlayer();
        MediaPlayer m = MediaPlayer.last;
        List<String> events = new ArrayList<>();

        p.prepared =
                v -> {
                    check(v == p, "prepared identity");
                    events.add("prepared");
                };
        p.completed =
                v -> {
                    check(v == p, "completion identity");
                    events.add("complete");
                };
        p.seek =
                v -> {
                    check(v == p, "seek identity");
                    events.add("seek");
                };
        throwsType(IllegalStateException.class, p::start);
        throwsType(IllegalStateException.class, p::prepareAsync);
        p.setOption(4, "start-on-prepared", 0);
        throwsType(IllegalArgumentException.class, () -> p.setOption(4, "start-on-prepared", 1));
        p.setDataSource("file:///silent.wav");
        check(p.getDataSource().equals("file:///silent.wav"), "source");
        p.prepareAsync();
        check(
                events.isEmpty() && m.starts == 0 && !p.isPlaying(),
                "prepare must not signal ready or autostart");
        throwsType(IllegalStateException.class, p::start);
        m.firePrepared();
        check(events.equals(List.of("prepared")) && m.starts == 0, "prepared callback only");
        p.start();
        check(p.isPlaying() && m.starts == 1, "start");
        check(p.getDuration() == 123456 && p.getCurrentPosition() == 0, "timing");
        p.pause();
        check(!p.isPlaying(), "pause");
        p.start();
        check(p.isPlaying(), "resume");
        p.seekTo(321);
        check(p.getCurrentPosition() == 321 && !events.contains("seek"), "pending seek");
        m.fireSeek();
        check(events.contains("seek"), "seek completion");
        throwsType(IllegalArgumentException.class, () -> p.seekTo(-1));
        throwsType(IllegalArgumentException.class, () -> p.seekTo(2147483648L));
        check(m.seekCalls == 1, "invalid seek not delegated");
        m.buffering.call(m, 76);
        check(p.buffering == 76, "buffer callback");
        m.video.call(m, 800, 600);
        check(p.width == 800 && p.height == 600 && p.sarN == 1 && p.sarD == 1, "video callback");
        m.fireComplete();
        check(!p.isPlaying() && Collections.frequency(events, "complete") == 1, "completion");
        p.reset();
        check(p.getDataSource() == null, "reset source");
        p.setDataSource("second");
        p.prepareAsync();
        m.firePrepared();
        check(Collections.frequency(events, "prepared") == 2, "reset retains OEM listener");
        p.error =
                (v, a, b) -> {
                    check(v == p && a == 1 && b == -38, "error payload");

                    return false;
                };
        check(
                !m.fireError(1, -38) && Collections.frequency(events, "complete") == 2,
                "unhandled error completes");
        p.reset();
        p.error = (v, a, b) -> true;
        check(
                m.fireError(7, 8) && Collections.frequency(events, "complete") == 2,
                "handled error suppresses completion");
        p.reset();
        p.error =
                (v, a, b) -> {
                    throw new IOException("OEM callback");
                };
        check(
                m.fireError(9, 10) && Collections.frequency(events, "complete") == 2,
                "throwing OEM error suppresses completion");
        p.info =
                (v, a, b) -> {
                    check(v == p && a == 3 && b == 4, "info identity");

                    return true;
                };
        check(m.info.call(m, 3, 4), "info result");
        p.info =
                (v, a, b) -> {
                    throw new IOException("info");
                };
        check(!m.info.call(m, 3, 4), "throwing info returns false");
        check(android.util.Log.errors == 2, "callback errors logged");
        p.reset();
        throwsType(IOException.class, () -> p.setDataSource("bad"));
        check(p.getDataSource() == null, "failed source not committed");
        p.setDataSource("third");
        p.prepareAsync();
        m.firePrepared();
        p.start();
        p.stop();
        check(m.state.equals("stopped"), "stop delegated");
        p.reset();
        p.release();
        check(p.getDataSource() == null && m.state.equals("released"), "release");
        int n = events.size();
        m.prepared.call(m);
        m.completion.call(m);
        m.seek.call(m);
        check(events.size() == n, "release clears OEM callbacks");
        p.release();
        check(m.state.equals("released"), "repeat release stays released");
        throwsType(IllegalStateException.class, p::start);
        throwsType(IllegalStateException.class, p::reset);
        System.out.println("PASS assertions=" + assertions);
    }
}
