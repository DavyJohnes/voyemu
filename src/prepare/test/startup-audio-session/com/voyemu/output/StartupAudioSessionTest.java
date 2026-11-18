package com.voyemu.output;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioManager;

import java.util.Arrays;

public final class StartupAudioSessionTest {
    static final class State implements SharedPreferences, SharedPreferences.Editor {
        String completed, pending;
        boolean commit = true;
        int commits;

        public String getString(String name, String fallback) {
            return completed == null ? fallback : completed;
        }

        public SharedPreferences.Editor edit() {
            return this;
        }

        public SharedPreferences.Editor putString(String name, String value) {
            pending = value;

            return this;
        }

        public boolean commit() {
            commits++;

            if (commit) {
                completed = pending;
            }

            return commit;
        }
    }

    static final class Owner extends Context {
        final AudioManager audio = new AudioManager();
        final State state = new State();

        public Object getSystemService(String name) {
            return audio;
        }

        public SharedPreferences getSharedPreferences(String name, int mode) {
            return state;
        }
    }

    static void require(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static void rejected(Runnable action) {
        try {
            action.run();
            throw new AssertionError("accepted invalid state");
        } catch (IllegalStateException expected) {
        }
    }

    public static void main(String[] args) {
        int streams = args.length == 0 ? 12 : Integer.parseInt(args[0]);

        require(streams == 11 || streams == 12);
        String boot = "12345678-1234-1234-1234-123456789abc",
                session = "0123456789abcdef0123456789abcdef";
        String key = StartupAudioPolicy.key(boot, session);
        Owner owner = new Owner();
        Arrays.fill(owner.audio.values, 5);
        StartupAudioPolicy.zero(owner, key);
        require(
                owner.audio.writes == streams
                        && owner.state.commits == 1
                        && Arrays.stream(owner.audio.values, 0, streams).allMatch(x -> x == 0));
        owner.audio.values[3] = 5;
        StartupAudioPolicy.zero(owner, key);
        require(
                owner.audio.values[3] == 5
                        && owner.audio.writes == streams
                        && owner.state.commits == 1);
        StartupAudioPolicy.zero(
                owner, StartupAudioPolicy.key(boot, "1123456789abcdef0123456789abcdef"));
        require(owner.audio.values[3] == 0 && owner.audio.writes == streams * 2);
        owner.audio.values[3] = 5;
        StartupAudioPolicy.zero(
                owner, StartupAudioPolicy.key("22345678-1234-1234-1234-123456789abc", session));
        require(owner.audio.values[3] == 0 && owner.audio.writes == streams * 3);
        Owner failing = new Owner();
        failing.audio.values[7] = 5;
        failing.audio.reject = 7;
        rejected(() -> StartupAudioPolicy.zero(failing, key));
        require(failing.state.completed == null && failing.state.commits == 0);
        failing.audio.reject = -1;
        StartupAudioPolicy.zero(failing, key);
        require(key.equals(failing.state.completed));
        Owner disk = new Owner();
        disk.state.commit = false;
        rejected(() -> StartupAudioPolicy.zero(disk, key));
        require(disk.state.completed == null);
        disk.state.commit = true;
        StartupAudioPolicy.zero(disk, key);
        require(disk.state.commits == 2);
        rejected(() -> StartupAudioPolicy.key(null, session));
        rejected(() -> StartupAudioPolicy.key(boot, ""));
        rejected(() -> StartupAudioPolicy.key(boot, session + "x"));
        rejected(() -> StartupAudioPolicy.key("not-a-boot", session));
        System.out.println(
                "PASS startup audio: duplicate/session/boot/readback retry/checkpoint"
                        + " failure/identity negatives");
    }
}
