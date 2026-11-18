package com.qinggan.canbus.service;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/** Bounded service-lifetime journal. Caller holds the service state lock. */
final class CanObservationJournal {
    static final class Record {
        final long seq;
        final String json;

        Record(long seq, String json) {
            this.seq = seq;
            this.json = json;
        }
    }

    private final int capacity;
    private final ArrayDeque<Record> records = new ArrayDeque<>();
    private long sequence;

    CanObservationJournal(int capacity) {
        if (capacity < 1) {
            throw new IllegalArgumentException("journal capacity");
        }

        this.capacity = capacity;
    }

    long next() {
        return sequence + 1;
    }

    void append(String json) {
        records.addLast(new Record(++sequence, json));

        if (records.size() > capacity) {
            records.removeFirst();
        }
    }

    long latest() {
        return sequence;
    }

    long oldest() {
        return records.isEmpty() ? sequence + 1 : records.getFirst().seq;
    }

    long dropped() {
        return oldest() - 1;
    }

    List<Record> after(long cursor, int limit) {
        List<Record> result = new ArrayList<>();

        for (Record record : records) {
            if (record.seq > cursor) {
                result.add(record);

                if (result.size() == limit) {
                    break;
                }
            }
        }

        return result;
    }
}
