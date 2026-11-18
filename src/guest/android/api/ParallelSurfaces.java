package com.voyemu.api;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.FutureTask;
import java.util.function.LongSupplier;

/** Both workers belong to one snapshot lease and finish before its owner can release it. */
final class ParallelSurfaces {
    interface Reader<T> {
        T read(int slot) throws Exception;
    }

    static final class Result<T> {
        final T value;
        final Exception error;
        final long started, completed;

        Result(T value, Exception error, long started, long completed) {
            this.value = value;
            this.error = error;
            this.started = started;
            this.completed = completed;
        }
    }

    private static <T> FutureTask<Result<T>> task(
            int slot, CountDownLatch started, LongSupplier now, Reader<T> reader) {
        return new FutureTask<>(() -> {
            started.countDown();
            started.await();
            long begin = now.getAsLong();

            try {
                return new Result<>(reader.read(slot), null, begin, now.getAsLong());
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: each surface worker hands its failure back as a
                // result
                return new Result<>(null, error, begin, now.getAsLong());
            }
        });
    }

    static long workerDeadline(long now, long deadline) {
        // Keep some of the original budget for the shared WindowManager and boot fences.
        return deadline - Math.min(1000, Math.max(0, deadline - now) / 4);
    }

    static <T> List<Result<T>> read(LongSupplier now, Reader<T> reader) throws Exception {
        CountDownLatch started = new CountDownLatch(2);
        FutureTask<Result<T>> primary = task(0, started, now, reader);
        FutureTask<Result<T>> secondary = task(1, started, now, reader);
        Thread[] workers = {
            new Thread(primary, "voyemu-ui-primary"), new Thread(secondary, "voyemu-ui-secondary")
        };

        for (Thread worker : workers) {
            worker.setDaemon(true);
        }

        boolean joined = false;
        boolean interrupted = false;

        try {
            for (Thread worker : workers) {
                worker.start();
            }

            for (Thread worker : workers) {
                worker.join();
            }

            joined = true;

            return Arrays.asList(primary.get(), secondary.get());
        } catch (InterruptedException error) {
            interrupted = true;
            throw error;
        } finally {
            if (!joined) {
                for (Thread worker : workers) {
                    worker.interrupt();
                }

                for (Thread worker : workers) {
                    while (worker.isAlive()) {
                        try {
                            worker.join();
                        } catch (InterruptedException error) {
                            interrupted = true;
                        }
                    }
                }
            }

            if (interrupted) {
                Thread.currentThread().interrupt();
            }
        }
    }
}
