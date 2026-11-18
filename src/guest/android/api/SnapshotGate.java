package com.voyemu.api;

import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

/** Serial snapshot access; queueing consumes the caller monotonic deadline. */
final class SnapshotGate {
    private final Semaphore permit = new Semaphore(1, true);

    void acquire(long deadline, LongSupplier clock) {
        long remaining = deadline - clock.getAsLong();

        if (remaining <= 0) {
            throw new IllegalStateException("deadline");
        }

        try {
            if (!permit.tryAcquire(remaining, TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException("deadline");
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("deadline", interrupted);
        }

        if (clock.getAsLong() >= deadline) {
            permit.release();
            throw new IllegalStateException("deadline");
        }
    }

    void release() {
        permit.release();
    }
}
