package com.voyemu.input;

import android.content.Context;
import android.media.AudioManager;

/** One-shot startup/save boundary; later user volume changes remain available. */
final class FreeStartupAudioPolicy {
    private FreeStartupAudioPolicy() {}

    static void zero(Context context) throws Exception {
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

        if (audio == null) {
            throw new IllegalStateException("free-wheel-audio-unavailable");
        }

        // API30 public getStreamMinVolume rejects private streams (including Bluetooth SCO).
        // Its internal entry point checks the same service minimum for all twelve streams.
        java.lang.reflect.Method minimum =
                AudioManager.class.getMethod("getStreamMinVolumeInt", int.class);

        for (int stream = 0; stream < 12; stream++) {
            if (((Integer) minimum.invoke(audio, stream)).intValue() != 0) {
                throw new IllegalStateException("free-wheel-audio-minimum");
            }
        }

        for (int stream = 0; stream < 12; stream++) {
            audio.setStreamVolume(stream, 0, 0);
        }

        for (int stream = 0; stream < 12; stream++) {
            if (audio.getStreamVolume(stream) != 0) {
                throw new IllegalStateException("free-wheel-audio-readback");
            }
        }
    }
}
