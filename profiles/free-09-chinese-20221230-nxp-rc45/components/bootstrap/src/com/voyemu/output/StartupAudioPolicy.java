package com.voyemu.output;

import static java.nio.charset.StandardCharsets.UTF_8;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioManager;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;

/** Resets playback once per owned cold boot/session; later volume changes are retained. */
final class StartupAudioPolicy {
    private StartupAudioPolicy() {}

    static void zero(Context context) {
        try {
            String session =
                    (String)
                            Class.forName("android.os.SystemProperties")
                                    .getMethod("get", String.class)
                                    .invoke(null, "qemu.voyemu.token");
            String boot;

            try (BufferedReader reader =
                    new BufferedReader(
                            new InputStreamReader(
                                    new FileInputStream("/proc/sys/kernel/random/boot_id"),
                                    UTF_8))) {
                boot = reader.readLine();
            }

            zero(context, key(boot, session));
        } catch (ReflectiveOperationException | IOException failure) {
            throw new IllegalStateException("startup-audio-identity", failure);
        }
    }

    static String key(String boot, String session) {
        if (boot == null
                || !boot.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")
                || session == null
                || !session.matches("[0-9a-f]{32}")) {
            throw new IllegalStateException("startup-audio-identity");
        }

        return boot + ":" + session;
    }

    static synchronized void zero(Context context, String key) {
        SharedPreferences state =
                context.getSharedPreferences("voyemu.startup-audio", Context.MODE_PRIVATE);

        if (key.equals(state.getString("completed", null))) {
            return;
        }

        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

        if (audio == null) {
            throw new IllegalStateException("startup-audio-unavailable");
        }

        for (int stream = 0; stream < 11; stream++) {
            audio.setStreamVolume(stream, 0, 0);
        }

        for (int stream = 0; stream < 11; stream++) {
            if (audio.getStreamVolume(stream) != 0) {
                throw new IllegalStateException("startup-audio-readback-" + stream);
            }
        }

        if (!state.edit().putString("completed", key).commit()) {
            throw new IllegalStateException("startup-audio-checkpoint");
        }
    }
}
