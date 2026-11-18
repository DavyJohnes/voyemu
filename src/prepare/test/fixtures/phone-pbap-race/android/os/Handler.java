package android.os;

import java.util.ArrayDeque;
import java.util.ArrayList;

public class Handler {
    private static final ArrayDeque<Runnable> QUEUE = new ArrayDeque<>();
    private static final ArrayList<Runnable> DELAYED = new ArrayList<>();
    private static final ArrayList<Long> DUE = new ArrayList<>();
    public static boolean immediate;

    public Handler(Looper value) {}

    public boolean post(Runnable value) {
        QUEUE.add(value);

        return true;
    }

    public boolean postDelayed(Runnable value, long delay) {
        DELAYED.add(value);
        DUE.add(SystemClock.now + delay);

        return true;
    }

    public Looper getLooper() {
        return Looper.MAIN;
    }

    public static void drain() {
        for (int n = 0; !QUEUE.isEmpty(); n++) {
            if (n > 1000) {
                throw new AssertionError("unbounded main queue");
            }

            QUEUE.remove().run();
        }
    }

    public static void advance(long millis) {
        SystemClock.now += millis;

        for (int i = DUE.size() - 1; i >= 0; i--) {
            if (DUE.get(i) <= SystemClock.now) {
                QUEUE.add(DELAYED.remove(i));
                DUE.remove(i);
            }
        }

        drain();
    }
}
