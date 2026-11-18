package com.voyemu.output;

import android.content.Context;

/** Boundary spy only: real policy is compiled and tested separately by run.cmake. */
final class StartupAudioPolicy {
    static int calls, failures;

    static void zero(Context context) {
        calls++;

        if (failures > 0) {
            failures--;
            throw new IllegalStateException("audio unavailable");
        }
    }
}
