package com.voyemu.prepare;

import com.qinggan.bluetooth.internal.ivt8.SyntheticPhonePeer;

import java.util.List;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicReference;

public final class SyntheticPhonePeerJournalConcurrencyTest {
    private static void verify(List<String> entries) {
        if (entries.size() > 64) {
            throw new AssertionError("journal exceeded limit: " + entries.size());
        }

        long previous = -1;

        for (String entry : entries) {
            if (entry == null) {
                throw new AssertionError("null journal entry");
            }

            long sequence = Long.parseLong(entry.substring(0, entry.indexOf(':')));

            if (previous != -1 && sequence != previous + 1) {
                throw new AssertionError("unordered or lost journal entry: " + entries);
            }

            previous = sequence;
        }
    }

    public static void main(String[] args) throws Exception {
        SyntheticPhonePeer peer = new SyntheticPhonePeer();
        int writers = 4;
        int rounds = 4000;
        CyclicBarrier start = new CyclicBarrier(writers + 1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread[] threads = new Thread[writers + 1];

        for (int worker = 0; worker < threads.length; worker++) {
            final int index = worker;
            threads[worker] =
                    new Thread(() -> {
                        try {
                            start.await();

                            for (int round = 0; round < rounds; round++) {
                                if (index == writers) {
                                    verify(peer.journal());
                                } else {
                                    peer.record("worker-" + index + "-" + round);
                                }
                            }
                        } catch (Throwable error) {
                            failure.compareAndSet(null, error);
                        }
                    });
            threads[worker].setDaemon(true);
            threads[worker].start();
        }

        for (Thread thread : threads) {
            thread.join(20000);

            if (thread.isAlive()) {
                throw new AssertionError("journal worker did not finish");
            }
        }

        if (failure.get() != null) {
            throw new AssertionError("concurrent journal failed", failure.get());
        }

        List<String> snapshot = peer.journal();
        verify(snapshot);

        if (peer.sequence() != (long) writers * rounds
                || snapshot.size() != 64
                || !snapshot.get(63).startsWith(peer.sequence() + ":")) {
            throw new AssertionError("journal lost latest events");
        }

        peer.record("after-snapshot");
        verify(snapshot);
        snapshot.clear();

        if (peer.journal().size() != 64) {
            throw new AssertionError("snapshot modified journal");
        }

        System.out.println("SyntheticPhonePeerJournalConcurrencyTest PASS");
    }
}
