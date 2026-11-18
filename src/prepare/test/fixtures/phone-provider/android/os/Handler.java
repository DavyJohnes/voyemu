package android.os;

public class Handler {
    public static Runnable queued;
    public static boolean hold, async;
    public static Thread worker;

    public Handler(Looper l) {}

    public boolean post(Runnable r) {
        if (hold) {
            queued = r;
        } else if (async) {
            worker =
                    new Thread(() -> {
                        Looper.isMain = true;

                        try {
                            r.run();
                        } finally {
                            Looper.isMain = false;
                        }
                    });
            worker.start();
        } else {
            boolean old = Looper.isMain;
            Looper.isMain = true;

            try {
                r.run();
            } finally {
                Looper.isMain = old;
            }
        }

        return true;
    }
}
