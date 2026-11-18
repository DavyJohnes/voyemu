package com.voyemu.api;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public final class SnapshotGateTest {
    private static long now() {
        return System.nanoTime() / 1000000;
    }

    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    private static void await(CountDownLatch latch) throws Exception {
        check(latch.await(2, TimeUnit.SECONDS));
    }

    private static void deadline(Runnable action) {
        try {
            action.run();
            throw new AssertionError("acquired past deadline");
        } catch (IllegalStateException expected) {
            check("deadline".equals(expected.getMessage()));
        }
    }

    public static void main(String[] args) throws Exception {
        SnapshotGate gate = new SnapshotGate();

        gate.acquire(now() + 2000, SnapshotGateTest::now);
        CountDownLatch started = new CountDownLatch(1), acquired = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread reader =
                new Thread(() -> {
                    started.countDown();

                    try {
                        gate.acquire(now() + 2000, SnapshotGateTest::now);

                        try {
                            acquired.countDown();
                        } finally {
                            gate.release();
                        }
                    } catch (Throwable error) {
                        // checkstyle-allow IllegalCatch: the test thread hands any failure back to
                        // the test
                        failure.set(error);
                    }
                });
        reader.start();
        await(started);
        check(!acquired.await(30, TimeUnit.MILLISECONDS));
        gate.release();
        await(acquired);
        reader.join(2000);
        check(!reader.isAlive());
        check(failure.get() == null);
        gate.acquire(now() + 2000, SnapshotGateTest::now);
        deadline(() -> gate.acquire(now() + 30, SnapshotGateTest::now));
        deadline(() -> gate.acquire(now() + 30, SnapshotGateTest::now));
        CountDownLatch waiting = new CountDownLatch(1);
        Thread interrupted =
                new Thread(() -> {
                    waiting.countDown();

                    try {
                        deadline(() -> gate.acquire(now() + 2000, SnapshotGateTest::now));
                        check(Thread.currentThread().isInterrupted());
                    } catch (Throwable error) {
                        // checkstyle-allow IllegalCatch: the test thread hands any failure back to
                        // the test
                        failure.set(error);
                    }
                });
        interrupted.start();
        await(waiting);
        interrupted.interrupt();
        interrupted.join(2000);
        check(!interrupted.isAlive());
        check(failure.get() == null);
        deadline(() -> gate.acquire(now() + 30, SnapshotGateTest::now));
        gate.release();
        deadline(() -> gate.acquire(now(), SnapshotGateTest::now));
        gate.acquire(now() + 2000, SnapshotGateTest::now);
        gate.release();
        // Time may expire just after a successful semaphore acquisition; return the permit.
        java.util.concurrent.atomic.AtomicInteger ticks =
                new java.util.concurrent.atomic.AtomicInteger();
        deadline(() -> gate.acquire(1, () -> ticks.getAndIncrement() == 0 ? 0 : 1));
        gate.acquire(now() + 2000, SnapshotGateTest::now);
        gate.release();
        System.out.println("snapshot queue/release/timeout/interruption/deadline passed");
    }
}
