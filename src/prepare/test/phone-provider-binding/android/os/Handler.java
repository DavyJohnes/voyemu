package android.os;

/** Only Android public constructor/main-post requirements; controlled QUEUE for deadline tests. */
public final class Handler {
    public interface Callback {}

    public static boolean hold;
    private static final java.util.Queue<Runnable> QUEUE =
            new java.util.concurrent.ConcurrentLinkedQueue<>();

    public Handler(Callback callback) {
        if (Looper.myLooper() == null) {
            throw new IllegalStateException("boundary-no-current-looper");
        }
    }

    public Handler(Looper looper) {
        if (looper != Looper.MAIN) {
            throw new AssertionError();
        }
    }

    public boolean post(Runnable task) {
        if (hold) {
            QUEUE.add(task);
        } else {
            new Thread(
                            () -> {
                                Looper.enterMain();

                                try {
                                    task.run();
                                } finally {
                                    Looper.leave();
                                }
                            },
                            "fixture-application-main")
                    .start();
        }

        return true;
    }

    public static void drainMain() {
        Looper.enterMain();

        try {
            for (Runnable r; (r = QUEUE.poll()) != null; ) {
                r.run();
            }
        } finally {
            Looper.leave();
        }
    }
}
