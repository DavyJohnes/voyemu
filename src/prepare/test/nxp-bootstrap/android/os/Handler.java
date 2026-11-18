package android.os;

import java.util.ArrayDeque;

public class Handler {
    public record Pending(Runnable task, long delay) {}

    public static final ArrayDeque<Pending> QUEUE = new ArrayDeque<>();

    public Handler(Looper looper) {}

    public boolean postDelayed(Runnable task, long delay) {
        QUEUE.add(new Pending(task, delay));

        return true;
    }

    public void removeCallbacksAndMessages(Object token) {
        QUEUE.clear();
    }
}
