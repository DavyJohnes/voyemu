package com.qinggan.audiopolicy;

import android.content.Context;

/** The launch reset has its own test; the service test starts from the reset volumes. */
final class StartupAudioPolicy {
    private StartupAudioPolicy() {}

    static void zero(Context context) {}
}
