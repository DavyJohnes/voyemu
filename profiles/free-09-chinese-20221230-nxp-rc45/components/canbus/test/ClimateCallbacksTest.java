package com.qinggan.canbus.service;

import java.util.ArrayDeque;
import java.util.concurrent.atomic.AtomicBoolean;

public final class ClimateCallbacksTest {
    public static void main(String[] args) throws Exception {
        Object binderMonitor = new Object();
        ArrayDeque<Runnable> main = new ArrayDeque<>();
        ClimateCallbacks<Integer> callbacks = new ClimateCallbacks<>(main::add);
        AtomicBoolean delivered = new AtomicBoolean(), reentered = new AtomicBoolean();

        synchronized (binderMonitor) {
            callbacks.enqueue(
                    225,
                    snapshot -> {
                        if (snapshot != 225) {
                            throw new AssertionError("Snapshot changed");
                        }

                        Thread remoteGetter =
                                new Thread(() -> {
                                    synchronized (binderMonitor) {
                                        reentered.set(true);
                                    }
                                });
                        remoteGetter.start();

                        try {
                            remoteGetter.join(1000);
                        } catch (InterruptedException e) {
                            throw new AssertionError(e);
                        }

                        if (remoteGetter.isAlive()) {
                            throw new AssertionError(
                                    "Callback deadlocked remote getter on Binder monitor");
                        }

                        delivered.set(true);
                    });

            if (delivered.get()) {
                throw new AssertionError("Callback ran inside Binder transaction");
            }
        }

        if (main.size() != 1) {
            throw new AssertionError("Callback not queued on main dispatcher");
        }

        main.remove().run();

        if (!delivered.get() || !reentered.get()) {
            throw new AssertionError("Reentrant delivery lost");
        }

        StringBuilder order = new StringBuilder();
        callbacks.enqueue(1, s -> order.append(s));
        callbacks.enqueue(2, s -> order.append(s));

        while (!main.isEmpty()) {
            main.remove().run();
        }

        if (!order.toString().equals("12")) {
            throw new AssertionError("Snapshot ordering");
        }

        System.out.println(
                "ClimateCallbacksTest PASS: deferred dispatch, remote getter reentry, snapshot"
                        + " order");
    }
}
