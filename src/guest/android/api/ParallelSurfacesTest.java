package com.voyemu.api;

import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class ParallelSurfacesTest {
    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    public static void main(String[] args) throws Exception {
        CountDownLatch bothReading = new CountDownLatch(2);
        IllegalStateException missingDisplay = new IllegalStateException("display_unavailable");
        List<ParallelSurfaces.Result<String>> result =
                ParallelSurfaces.read(
                        System::nanoTime,
                        slot -> {
                            bothReading.countDown();
                            assert bothReading.await(2, TimeUnit.SECONDS)
                                    : "both surface reads must be in flight";

                            if (slot == 0) {
                                throw missingDisplay;
                            }

                            return "secondary";
                        });

        assert result.get(0).error == missingDisplay && result.get(0).value == null;
        assert result.get(1).error == null && "secondary".equals(result.get(1).value);
        assert Math.max(result.get(0).started, result.get(1).started)
                <= Math.min(result.get(0).completed, result.get(1).completed);

        assert ParallelSurfaces.workerDeadline(1000, 1800) == 1600;
        assert ParallelSurfaces.workerDeadline(1000, 7000) == 6000;
        assert ParallelSurfaces.workerDeadline(2000, 1000) == 1000;
        CountDownLatch healthy = new CountDownLatch(1);
        AtomicInteger time = new AtomicInteger(1000);
        List<ParallelSurfaces.Result<Integer>> stalled =
                ParallelSurfaces.read(
                        time::get,
                        slot -> {
                            if (slot == 1) {
                                healthy.countDown();

                                return slot;
                            }

                            assert healthy.await(2, TimeUnit.SECONDS);
                            time.set((int) ParallelSurfaces.workerDeadline(time.get(), 1800));
                            throw new IllegalStateException("deadline");
                        });
        assert "deadline".equals(stalled.get(0).error.getMessage())
                && stalled.get(0).completed == 1600;
        assert stalled.get(1).error == null && stalled.get(1).value == 1;

        CountDownLatch inFlight = new CountDownLatch(2);
        CountDownLatch never = new CountDownLatch(1);
        AtomicInteger exited = new AtomicInteger();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread owner =
                new Thread(() -> {
                    try {
                        ParallelSurfaces.read(
                                System::nanoTime,
                                slot -> {
                                    inFlight.countDown();

                                    try {
                                        never.await();

                                        return slot;
                                    } finally {
                                        exited.incrementAndGet();
                                    }
                                });
                        throw new AssertionError("interrupted lease owner returned a tree");
                    } catch (InterruptedException expected) {
                        if (exited.get() != 2 || !Thread.currentThread().isInterrupted()) {
                            failure.set(
                                    new AssertionError(
                                            "lease owner left before both reads stopped"));
                        }
                    } catch (Throwable error) {
                        // checkstyle-allow IllegalCatch: the test thread hands any failure back to
                        // the test
                        failure.set(error);
                    }
                });
        owner.setDaemon(true);
        owner.start();
        assert inFlight.await(2, TimeUnit.SECONDS);
        owner.interrupt();
        owner.join(2000);
        assert !owner.isAlive();
        assert failure.get() == null : failure.get();
    }
}
