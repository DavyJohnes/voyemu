package android.os;

public class Handler {
    public boolean accept = true, run = true;
    public int posts, removals;
    public long delayMillis;
    public Runnable pending;

    public Looper getLooper() {
        return Looper.MAIN;
    }

    public void removeCallbacks(Runnable task) {
        removals++;
    }

    public boolean post(Runnable task) {
        posts++;
        pending = task;

        if (delayMillis > 0) {
            try {
                Thread.sleep(delayMillis);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }

        if (accept && run) {
            task.run();
        }

        return accept;
    }
}
