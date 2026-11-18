package com.voyemu.api;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/** Immutable safe payload journal. Readers own their cursors; eviction is explicit. */
final class EventJournal {
    static final class Entry {
        final long sequence, time;
        final String payload;

        Entry(long sequence, long time, String payload) {
            this.sequence = sequence;
            this.time = time;
            this.payload = payload;
        }
    }

    private final ArrayDeque<Entry> entries = new ArrayDeque<>();
    private final int capacity, byteLimit;
    private int bytes;
    private long sequence;
    private boolean closed;

    EventJournal(int capacity, int byteLimit) {
        this.capacity = capacity;
        this.byteLimit = byteLimit;
    }

    synchronized long watermark() {
        return sequence;
    }

    synchronized void append(long time, String payload) {
        if (closed) {
            return;
        }

        // Even an oversized dropped event advances the sequence so readers see a gap.
        Entry entry = new Entry(++sequence, time, payload);

        if (payload.length() * 2 <= byteLimit) {
            entries.addLast(entry);
            bytes += payload.length() * 2;
        }

        while (entries.size() > capacity || bytes > byteLimit) {
            bytes -= entries.removeFirst().payload.length() * 2;
        }

        notifyAll();
    }

    synchronized boolean gap(long cursor) {
        return cursor < (entries.isEmpty() ? sequence : entries.getFirst().sequence - 1);
    }

    synchronized List<Entry> read(long cursor, long waitMillis) throws InterruptedException {
        if (cursor < 0 || cursor > sequence || waitMillis < 0 || waitMillis > 30000) {
            throw new IllegalArgumentException("cursor_bounds");
        }

        long end = System.nanoTime() + waitMillis * 1000000;

        while (!closed && sequence <= cursor && waitMillis > 0) {
            wait(waitMillis);
            waitMillis = Math.max(0, (end - System.nanoTime()) / 1000000);
        }

        List<Entry> result = new ArrayList<>();

        for (Entry entry : entries) {
            if (entry.sequence > cursor) {
                result.add(entry);

                if (result.size() == 256) {
                    break;
                }
            }
        }

        return result;
    }

    synchronized void close() {
        closed = true;
        notifyAll();
    }
}
