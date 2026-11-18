package com.qinggan.keymanager.service.engine;

import android.view.KeyEvent;

/** Boundary invocation double only; no copied OEM phase classifier or timer. */
public final class KeyManagerReader {
    public int calls;
    public KeyEvent event;
    public boolean result = true;
    public RuntimeException failure;

    public boolean onKeyEvent(KeyEvent value) {
        calls++;
        event = value;

        if (failure != null) {
            throw failure;
        }

        return result;
    }
}
