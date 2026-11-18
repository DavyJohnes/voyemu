package android.os;

import java.util.concurrent.ConcurrentLinkedQueue;

public class Handler {
    private static final ConcurrentLinkedQueue<Runnable> QUEUE =
            new ConcurrentLinkedQueue<Runnable>();
    public static volatile boolean immediate;
    private static final java.util.concurrent.ExecutorService MAIN_EXECUTOR =
            java.util.concurrent.Executors.newSingleThreadExecutor(task -> {
                Thread t = new Thread(task, "FixtureAndroidMain");
                t.setDaemon(true);

                return t;
            });
    private final PeerTestRuntime.Identity owner = PeerTestRuntime.CURRENT.get();

    public Handler(Looper looper) {}

    public boolean post(final Runnable runnable) {
        Runnable owned =
                () -> {
                    PeerTestRuntime.Identity prior = PeerTestRuntime.CURRENT.get();
                    PeerTestRuntime.CURRENT.set(owner);

                    try {
                        synchronized (PeerTestRuntime.MAIN_LOCK) {
                            runnable.run();
                        }
                    } finally {
                        PeerTestRuntime.CURRENT.set(prior);
                    }
                };

        if (immediate) {
            if ("FixtureAndroidMain".equals(Thread.currentThread().getName())) {
                owned.run();
            } else {
                try {
                    MAIN_EXECUTOR.submit(owned).get(2, java.util.concurrent.TimeUnit.SECONDS);
                } catch (Exception failed) {
                    throw new AssertionError("main scheduling", failed);
                }
            }
        } else {
            QUEUE.add(owned);
        }

        return true;
    }

    public boolean postDelayed(final Runnable runnable, long delay) {
        java.util.concurrent.CompletableFuture.delayedExecutor(
                        delay, java.util.concurrent.TimeUnit.MILLISECONDS)
                .execute(() -> post(runnable));

        return true;
    }

    public Looper getLooper() {
        return Looper.MAIN;
    }

    public static void drain() {
        for (int count = 0; !QUEUE.isEmpty(); count++) {
            if (count > 1000) {
                throw new AssertionError("unbounded main queue");
            }

            Runnable task = QUEUE.poll();

            if (task != null) {
                task.run();
            }
        }
    }
}
