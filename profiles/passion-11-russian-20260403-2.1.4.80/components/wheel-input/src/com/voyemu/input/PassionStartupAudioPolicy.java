package com.voyemu.input;

import android.content.Context;
import android.media.AudioManager;

/** One-shot startup/save boundary; later user volume changes remain available. */
final class PassionStartupAudioPolicy {
    private PassionStartupAudioPolicy() {}

    static void zero(Context context) throws Exception {
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

        if (audio == null) {
            throw new IllegalStateException("passion-wheel-audio-unavailable");
        }

        // Read back the result instead of depending on a hidden minimum-volume API.
        for (int stream = 0; stream < 12; stream++) {
            audio.setStreamVolume(stream, 0, 0);
        }

        for (int stream = 0; stream < 12; stream++) {
            if (audio.getStreamVolume(stream) != 0) {
                throw new IllegalStateException("passion-wheel-audio-readback");
            }
        }
    }
}
