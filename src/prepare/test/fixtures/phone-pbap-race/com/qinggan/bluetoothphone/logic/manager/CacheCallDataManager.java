package com.qinggan.bluetoothphone.logic.manager;

import java.util.ArrayList;
import java.util.List;

/**
 * Faithful finite OEM two-worker ordering fragment; no ContentProvider substitute or production
 * artifact.
 */
public final class CacheCallDataManager {
    private static final CacheCallDataManager INSTANCE = new CacheCallDataManager();
    private boolean isCallLogInserting;
    private final List<String> contacts = new ArrayList<>(), callLogs = new ArrayList<>();
    private Runnable contactWorker;

    public static synchronized CacheCallDataManager getInstance() {
        return INSTANCE;
    }

    public void insertCallLog() {
        if (isCallLogInserting) {
            return;
        }

        isCallLogInserting = true;
        callLogs.clear();
    }

    public void insertContacts(List<String> values) {
        final List<String> copy = new ArrayList<>(values);

        contactWorker = () -> contacts.addAll(copy);
    }

    public void runContacts() {
        if (contactWorker != null) {
            Runnable task = contactWorker;
            contactWorker = null;
            task.run();
        }
    }

    public void workerFirstDelete() {
        callLogs.clear();
    }

    public void workerFinalDelete() {
        contacts.clear();
        isCallLogInserting = false;
    }

    public int rows() {
        return contacts.size();
    }

    public boolean workerRunning() {
        return isCallLogInserting;
    }

    public int historyRows() {
        return callLogs.size();
    }
}
