package com.qinggan.audiopolicy;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioManager;

import java.io.BufferedReader;
import java.io.FileReader;

/** Resets playback once per owned launch; later volume changes are retained. */
final class StartupAudioPolicy {
    private StartupAudioPolicy() {}

    static String boot() {
        try (BufferedReader reader =
                new BufferedReader(new FileReader("/proc/sys/kernel/random/boot_id"))) {
            return reader.readLine();
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: Identity read failures are reported as
            // startup-audio-identity.
            throw new IllegalStateException("startup-audio-identity", failure);
        }
    }

    static void zero(Context context) {
        ensureBoot(context, boot());
    }

    static synchronized void ensureBoot(Context context, String boot) {
        SharedPreferences state = state(context);
        // A host handshake supersedes the immutable qemu token restored with a snapshot.
        String session = state.getString("host-session", null);
        String generation = state.getString("host-generation", null);

        if (boot.equals(state.getString("host-boot", null))
                && session != null
                && generation != null
                && (key(boot, session) + ":" + generation)
                        .equals(state.getString("completed", null))) {
            return;
        }

        try {
            String token =
                    (String)
                            Class.forName("android.os.SystemProperties")
                                    .getMethod("get", String.class)
                                    .invoke(null, "qemu.voyemu.token");
            zero(context, key(boot, token));
        } catch (RuntimeException failure) {
            // checkstyle-allow IllegalCatch: Preserve unchecked startup failures before wrapping
            // reflection failures.
            throw failure;
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: Reflective property lookup failures report
            // startup-audio-identity.
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

    private static SharedPreferences state(Context context) {
        return context.getSharedPreferences("voyemu.startup-audio", Context.MODE_PRIVATE);
    }

    static synchronized void zero(Context context, String key) {
        SharedPreferences state = state(context);

        if (key.equals(state.getString("completed", null))) {
            return;
        }

        reset(context);
        checkpoint(state, state.edit().putString("completed", key));
    }

    static synchronized void launch(
            Context context, String boot, String session, long generation, boolean force) {
        String key = key(boot, session) + ":" + generation;

        if (generation <= 0) {
            throw new IllegalArgumentException("startup-audio-generation");
        }

        SharedPreferences state = state(context);
        boolean sameOwner =
                boot.equals(state.getString("host-boot", null))
                        && session.equals(state.getString("host-session", null));
        String previous = state.getString("host-generation", null);

        if (sameOwner && previous != null && generation < Long.parseLong(previous)) {
            throw new IllegalStateException("startup-audio-stale-generation");
        }

        if (!force && key.equals(state.getString("completed", null))) {
            return;
        }

        reset(context);
        checkpoint(
                state,
                state.edit()
                        .putString("completed", key)
                        .putString("host-boot", boot)
                        .putString("host-session", session)
                        .putString("host-generation", Long.toString(generation)));
    }

    static synchronized boolean completed(
            Context context, String boot, String session, long generation) {
        return (key(boot, session) + ":" + generation)
                .equals(state(context).getString("completed", null));
    }

    private static void checkpoint(SharedPreferences state, SharedPreferences.Editor editor) {
        if (!editor.commit()) {
            // commit(false) still updates the process cache; invalidate it before retry.
            state.edit().remove("completed").commit();
            throw new IllegalStateException("startup-audio-checkpoint");
        }
    }

    private static void reset(Context context) {
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

        SharedPreferences.Editor volumes =
                context.getSharedPreferences("oem_audio_volume", Context.MODE_PRIVATE).edit();

        for (int stream = 0; stream <= 24; stream++) {
            volumes.putInt("stream-" + stream, 0);
        }

        if (!volumes.commit()) {
            throw new IllegalStateException("startup-oem-volume-checkpoint");
        }
    }
}
