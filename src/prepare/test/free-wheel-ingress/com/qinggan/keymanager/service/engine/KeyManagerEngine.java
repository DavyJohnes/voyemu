package com.qinggan.keymanager.service.engine;

import android.content.Context;

import java.util.List;

/** Own dispatch double. The actual OEM reader is verified by DEX inverse preservation. */
public final class KeyManagerEngine {
    // Read reflectively by the production code under test.
    @SuppressWarnings("UnusedVariable")
    private List<Object> mModules;

    private final Context context;
    public int legacyCalls;
    public KeyManagerMessage legacyMessage;
    public boolean result = true;
    public RuntimeException failure;

    public KeyManagerEngine(Context c, Object reader) {
        context = c;
        mModules = List.of(reader);
    }

    public Context getContext() {
        return context;
    }

    public void modules(List<Object> value) {
        mModules = value;
    }

    public boolean pushMsg(KeyManagerMessage m) {
        legacyCalls++;
        legacyMessage = m;

        if (failure != null) {
            throw failure;
        }

        return result;
    }
}
