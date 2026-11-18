package com.voyemu.input;

import java.util.LinkedHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;

public final class FreeKeySubmissionTest {
    private static void check(boolean yes, String message) {
        if (!yes) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        FreeKeySubmission normal = new FreeKeySubmission(1000);

        normal.submit(() -> null);
        normal.submit(() -> null);
        FreeKeyInputSession session =
                new FreeKeyInputSession(
                        "free_test", "0123456789abcdef0123456789abcdef", 0, new LinkedHashMap<>());
        var holds = new LinkedHashMap<String, String>();
        FreeKeyInputSession.Watermark save =
                (n, h) -> {
                    holds.clear();
                    holds.putAll(h);
                };
        var down = session.accept(1, "photo", "down", "pointer", save);
        session.confirmed(down, save);
        session.accept(2, "photo", "cancel", "pointer", save);
        CountDownLatch started = new CountDownLatch(1),
                finish = new CountDownLatch(1),
                done = new CountDownLatch(1);
        AtomicBoolean interrupted = new AtomicBoolean(), second = new AtomicBoolean();
        FreeKeySubmission uncertain = new FreeKeySubmission(100);

        try {
            uncertain.submit(() -> {
                started.countDown();

                try {
                    finish.await();
                } catch (InterruptedException failure) {
                    interrupted.set(true);
                }

                done.countDown();

                return null;
            });
            throw new AssertionError("expected timeout");
        } catch (TimeoutException expected) {
        }

        check(started.await(1, TimeUnit.SECONDS), "operation never started");

        try {
            uncertain.submit(() -> {
                second.set(true);

                return null;
            });
            throw new AssertionError("second operation admitted");
        } catch (IllegalStateException expected) {
        }

        check(
                session.pendingSubmission() && session.hasHolds() && holds.containsKey("photo"),
                "uncertain release lost durable hold");
        finish.countDown();
        check(done.await(1, TimeUnit.SECONDS), "operation interrupted or abandoned");
        check(
                !interrupted.get() && !second.get(),
                "timeout canceled Binder or submitted second operation");

        try {
            uncertain.submit(() -> null);
            throw new AssertionError("late completion silently cleared uncertainty");
        } catch (IllegalStateException expected) {
        }

        var restored =
                new FreeKeyInputSession(
                        "free_test", "0123456789abcdef0123456789abcdef", 2, holds, true);

        try {
            restored.accept(3, "photo", "cancel", "pointer", save);
            throw new AssertionError("restart replay accepted");
        } catch (IllegalArgumentException expected) {
        }

        System.out.println(
                "Real running transaction timeout: no cancel, no second submission, durable release"
                        + " hold/restart PASS");
    }
}
