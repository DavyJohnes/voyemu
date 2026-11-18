package android.os;

import java.util.ArrayDeque;

/** The main thread as a queue the test drains. */
public final class Looper {
    private static final Looper MAIN = new Looper();
    final ArrayDeque<Runnable> queue = new ArrayDeque<>();

    public static Looper getMainLooper() {
        return MAIN;
    }

    /** Runs the posted messages, including messages they post, in order. */
    public static void drain() {
        while (!MAIN.queue.isEmpty()) {
            MAIN.queue.removeFirst().run();
        }
    }
}
