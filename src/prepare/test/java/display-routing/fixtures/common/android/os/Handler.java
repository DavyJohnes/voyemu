package android.os;

import java.util.ArrayDeque;
import java.util.Queue;

public class Handler {
    static final Queue<Runnable> JOBS = new ArrayDeque<>();

    public Handler(Looper l) {}

    public boolean post(Runnable r) {
        JOBS.add(r);

        return true;
    }

    public static void drain() {
        Looper.main = true;

        while (!JOBS.isEmpty()) {
            JOBS.remove().run();
        }
    }
}
