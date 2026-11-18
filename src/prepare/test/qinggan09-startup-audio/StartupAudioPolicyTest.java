package com.qinggan.audiopolicy;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioManager;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public final class StartupAudioPolicyTest {
    private static int checks;

    private static void need(boolean value, String name) {
        checks++;

        if (!value) {
            throw new AssertionError(name);
        }
    }

    private static void rejected(Runnable action) {
        try {
            action.run();
            throw new AssertionError("accepted failure");
        } catch (IllegalStateException expected) {
            checks++;
        }
    }

    static final class State implements SharedPreferences {
        final Map<String, Object> values = new HashMap<>(), persisted = new HashMap<>();
        boolean writable = true;
        int commits;

        public int getInt(String name, int fallback) {
            return (Integer) values.getOrDefault(name, fallback);
        }

        public String getString(String name, String fallback) {
            return (String) values.getOrDefault(name, fallback);
        }

        public Editor edit() {
            return new Editor() {
                final Map<String, Object> pending = new HashMap<>();

                public Editor putString(String name, String value) {
                    pending.put(name, value);

                    return this;
                }

                public Editor putInt(String name, int value) {
                    pending.put(name, value);

                    return this;
                }

                public Editor remove(String name) {
                    pending.put(name, null);

                    return this;
                }

                public boolean commit() {
                    commits++;

                    // Android updates its memory cache before attempting disk persistence.
                    for (Map.Entry<String, Object> entry : pending.entrySet()) {
                        if (entry.getValue() == null) {
                            values.remove(entry.getKey());
                        } else {
                            values.put(entry.getKey(), entry.getValue());
                        }
                    }

                    if (writable) {
                        persisted.clear();
                        persisted.putAll(values);
                    }

                    return writable;
                }
            };
        }
    }

    static final class Owner extends Context {
        final AudioManager audio = new AudioManager();
        final State identity = new State(), volumes = new State();

        public Object getSystemService(String name) {
            return audio;
        }

        public SharedPreferences getSharedPreferences(String name, int mode) {
            if ("voyemu.startup-audio".equals(name)) {
                return identity;
            }

            if ("oem_audio_volume".equals(name)) {
                return volumes;
            }

            throw new AssertionError(name);
        }
    }

    public static void main(String[] args) throws Exception {
        String boot = "12345678-1234-1234-1234-123456789abc";
        String session = "0123456789abcdef0123456789abcdef";
        String key = StartupAudioPolicy.key(boot, session);
        Owner owner = new Owner();

        Arrays.fill(owner.audio.values, 8);

        for (int stream = 0; stream <= 24; stream++) {
            owner.volumes.values.put("stream-" + stream, 15);
        }

        StartupAudioPolicy.zero(owner, key);
        need(
                Arrays.stream(owner.audio.values, 0, 11).allMatch(x -> x == 0),
                "all Android playback channels");
        need(owner.audio.values[11] == 8, "API28 has eleven streams");

        for (int stream = 0; stream <= 24; stream++) {
            need(
                    Integer.valueOf(0).equals(owner.volumes.values.get("stream-" + stream)),
                    "OEM channel " + stream);
        }

        owner.audio.values[3] = 5;
        owner.volumes.values.put("stream-3", 15);
        StartupAudioPolicy.zero(owner, key);
        need(
                owner.audio.values[3] == 5
                        && Integer.valueOf(15).equals(owner.volumes.values.get("stream-3")),
                "later user volume retained");
        need(
                owner.audio.writes == 11 && owner.identity.commits == 1,
                "duplicate callback/service restart same identity");
        StartupAudioPolicy.zero(
                owner, StartupAudioPolicy.key(boot, "1123456789abcdef0123456789abcdef"));
        need(
                owner.audio.values[3] == 0
                        && Integer.valueOf(0).equals(owner.volumes.values.get("stream-3")),
                "warm session resets platform and OEM");
        owner.audio.values[3] = 5;
        StartupAudioPolicy.zero(
                owner, StartupAudioPolicy.key("22345678-1234-1234-1234-123456789abc", session));
        need(owner.audio.values[3] == 0, "cold boot resets");
        Owner failing = new Owner();
        failing.audio.values[7] = 5;
        failing.audio.reject = 7;
        rejected(() -> StartupAudioPolicy.zero(failing, key));
        need(
                failing.identity.values.isEmpty() && failing.volumes.commits == 0,
                "no checkpoint after mixer refusal");
        failing.audio.reject = -1;
        StartupAudioPolicy.zero(failing, key);
        need(key.equals(failing.identity.values.get("completed")), "retry succeeds");
        Owner oemFailure = new Owner();
        oemFailure.volumes.writable = false;
        rejected(() -> StartupAudioPolicy.zero(oemFailure, key));
        need(oemFailure.identity.values.isEmpty(), "OEM commit must precede completion");
        oemFailure.volumes.writable = true;
        StartupAudioPolicy.zero(oemFailure, key);
        need(key.equals(oemFailure.identity.values.get("completed")), "OEM retry succeeds");
        Owner diskFailure = new Owner();
        diskFailure.identity.writable = false;
        rejected(() -> StartupAudioPolicy.zero(diskFailure, key));
        need(
                diskFailure.identity.values.isEmpty() && diskFailure.identity.persisted.isEmpty(),
                "failed disk commit must invalidate in-memory completion");
        int attempts = diskFailure.identity.commits;
        diskFailure.identity.writable = true;
        StartupAudioPolicy.zero(diskFailure, key);
        need(diskFailure.identity.commits > attempts, "same-process retry persists completion");
        need(
                key.equals(diskFailure.identity.values.get("completed"))
                        && key.equals(diskFailure.identity.persisted.get("completed")),
                "identity commit retry persists same identity");
        rejected(() -> StartupAudioPolicy.key(null, session));
        rejected(() -> StartupAudioPolicy.key(boot, ""));
        rejected(() -> StartupAudioPolicy.key("not-a-boot", session));
        System.out.println("PASS A9 startup audio: " + checks + " checks");
    }
}
