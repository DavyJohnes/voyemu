package com.voyemu.input;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** One transaction at a time. Timeout is uncertainty, never cancellation or a replay permit. */
final class FreeKeySubmission {
    private final long timeoutMs;
    private final ThreadPoolExecutor worker =
            new ThreadPoolExecutor(
                    1,
                    1,
                    0,
                    TimeUnit.MILLISECONDS,
                    new ArrayBlockingQueue<Runnable>(1),
                    task -> {
                        Thread thread = new Thread(task, "voyemu-free-key-submit");
                        thread.setDaemon(true);

                        return thread;
                    });
    private Future<Void> inFlight;

    FreeKeySubmission(long timeoutMs) {
        if (timeoutMs < 1 || timeoutMs > 1000) {
            throw new IllegalArgumentException("free-key-submit-budget");
        }

        this.timeoutMs = timeoutMs;
    }

    synchronized void submit(Callable<Void> operation) throws Exception {
        if (inFlight != null) {
            throw new IllegalStateException("free-key-submission-uncertain");
        }

        inFlight = worker.submit(operation);
        inFlight.get(timeoutMs, TimeUnit.MILLISECONDS);
        inFlight = null;
    }
}
