package android.os;

import java.util.ArrayDeque;

/** Queues work for the test thread, which runs it with drain() regardless of the delay. */
public class Handler {
    private static final ArrayDeque<Runnable> QUEUE = new ArrayDeque<>();

    public Handler(Looper looper) {}

    public boolean post(Runnable work) {
        synchronized (QUEUE) {
            QUEUE.add(work);
        }

        return true;
    }

    public boolean postDelayed(Runnable work, long delayMillis) {
        return post(work);
    }

    /** Runs the queued work, including work it queues, and returns the number of tasks run. */
    public static int drain() {
        int count = 0;

        for (; ; ) {
            Runnable work;

            synchronized (QUEUE) {
                work = QUEUE.poll();
            }

            if (work == null) {
                return count;
            }

            work.run();
            count++;
        }
    }
}
