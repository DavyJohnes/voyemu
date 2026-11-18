package android.os;

import java.util.ArrayDeque;

public class Handler {
    private static final ArrayDeque<Runnable> QUEUE = new ArrayDeque<>();

    public Handler(Looper looper) {}

    public boolean post(Runnable task) {
        QUEUE.add(task);

        return true;
    }

    public Looper getLooper() {
        return Looper.MAIN;
    }

    public static void drain() {
        int count = 0;

        while (!QUEUE.isEmpty()) {
            if (++count > 100) {
                throw new AssertionError("unbounded callback queue");
            }

            QUEUE.remove().run();
        }
    }
}
