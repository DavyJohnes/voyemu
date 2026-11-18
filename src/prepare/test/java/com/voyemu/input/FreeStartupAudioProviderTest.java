package com.voyemu.input;

import android.media.AudioManager;
import android.os.Bundle;

import org.json.JSONObject;

import java.lang.reflect.Field;
import java.util.Arrays;

/** Executes the production provider call dispatcher with Android boundary doubles. */
public final class FreeStartupAudioProviderTest {
    private static final String ID = "free_audio_test", TOKEN = "0123456789abcdef0123456789abcdef";
    private static final String BOOT = "01234567-89ab-cdef-0123-456789abcdef";
    private static final String NONCE = "abcdef0123456789abcdef0123456789";

    private static void check(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    private static void field(Object target, String name, Object value) throws Exception {
        Field f = target.getClass().getDeclaredField(name);

        f.setAccessible(true);
        f.set(target, value);
    }

    private static Bundle request() {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putString("session", ID);
        b.putString("token", TOKEN);
        b.putString("save_nonce", NONCE);

        return b;
    }

    private static JSONObject success(Bundle result) {
        check(result.getString("error") == null, "unexpected error: " + result);

        return new JSONObject(result.getString("json"));
    }

    private static void denied(Bundle result) {
        check(result.getString("error") != null, "request accepted: " + result);
    }

    private static FreeKeyInputProvider provider() throws Exception {
        FreeKeyInputProvider p = new FreeKeyInputProvider();

        field(p, "storage", p.getContext().preferences);
        field(p, "connection", new FreeKeyConnection(p.getContext()));
        field(p, "boot", BOOT);

        return p;
    }

    private static void input(FreeKeyInputProvider p, long sequence, String phase) {
        Bundle b = request();

        b.putLong("sequence", sequence);
        b.putString("button", "volume_up");
        b.putString("phase", phase);
        b.putString("owner", "test");
        success(p.call("input", null, b));
    }

    public static void main(String[] args) throws Exception {
        AudioManager boundary = new AudioManager();

        for (int stream : new int[] {6, 7, 9, 11}) {
            try {
                boundary.getStreamMinVolume(stream);
                throw new AssertionError("private stream accepted: " + stream);
            } catch (IllegalArgumentException expected) {
                check(
                        expected.getMessage().equals("Invalid stream type " + stream),
                        "API30 public minimum rejection");
            }

            check(
                    boundary.getStreamMinVolumeInt(stream) == 0,
                    "service minimum includes private stream " + stream);
        }

        FreeKeyInputProvider p = provider();
        AudioManager a = p.getContext().audio;
        Arrays.fill(a.volume, 7);
        success(p.call("bootstrap", null, request()));
        check(
                a.writes == 12 && a.reads == 12 && a.minima == 12,
                "new session must zero and verify all twelve streams");
        Arrays.fill(a.volume, 5);
        int writes = a.writes;
        success(p.call("bootstrap", null, request()));
        check(
                a.writes == writes && a.volume[3] == 5,
                "same session bootstrap preserves chosen positive volume");
        Bundle bad = request();
        bad.putString("token", "00000000000000000000000000000000");
        denied(p.call("presave", null, bad));
        check(a.writes == writes, "authentication before audio");

        for (String nonce :
                new String[] {"", "ABCDEF0123456789ABCDEF0123456789", "abc", NONCE + "0"}) {
            bad = request();
            bad.putString("save_nonce", nonce);
            denied(p.call("presave", null, bad));
        }

        bad = request();
        bad.remove("save_nonce");
        denied(p.call("presave", null, bad));
        check(a.writes == writes, "nonce before audio");
        input(p, 1, "down");
        denied(p.call("presave", null, request()));
        check(a.writes == writes, "held input rejects save before audio");
        input(p, 2, "up");
        JSONObject receipt = success(p.call("presave", null, request()));
        check(
                receipt.getString("status").equals("presave-ready")
                        && receipt.getString("save_nonce").equals(NONCE)
                        && receipt.getString("session_id").equals(ID)
                        && receipt.getString("boot_id").equals(BOOT)
                        && Long.valueOf(2).equals(receipt.get("sequence"))
                        && Boolean.TRUE.equals(receipt.get("acoustic_zero"))
                        && Long.valueOf(1).equals(receipt.get("schema"))
                        && receipt.get("token") == null,
                "operation-bound direct receipt");
        check(!p.getContext().preferences.contains("presave"), "no durable presave receipt");
        denied(p.call("status", "presave", request()));
        Arrays.fill(a.volume, 9);
        check(a.volume[3] == 9, "no ongoing clamp");
        bad = request();
        bad.putString("save_nonce", "00000000000000000000000000000000");
        receipt = success(p.call("presave", null, bad));
        check(
                receipt.getString("save_nonce").equals(bad.getString("save_nonce")),
                "fresh nonce returned");
        a.minimum[11] = 1;
        writes = a.writes;
        denied(p.call("presave", null, request()));
        check(a.writes == writes, "nonzero minimum rejects before writes");
        a.minimum[11] = 0;
        a.failRead = 11;
        denied(p.call("presave", null, request()));
        denied(p.call("status", "presave", request()));
        a.failRead = -1;
        a.ignoreWrites = true;
        Arrays.fill(a.volume, 4);
        denied(p.call("presave", null, request()));
        a.ignoreWrites = false;
        Arrays.fill(a.volume, 6);
        field(p, "boot", "fedcba98-7654-3210-fedc-ba9876543210");
        success(p.call("bootstrap", null, request()));
        check(a.volume[11] == 0, "new boot zeros same session");
        FreeKeyInputProvider pending = provider();
        success(pending.call("bootstrap", null, request()));
        FreeKeyConnection.failSubmit = true;
        Bundle down = request();
        down.putLong("sequence", 1);
        down.putString("button", "photo");
        down.putString("phase", "down");
        down.putString("owner", "test");
        denied(pending.call("input", null, down));
        FreeKeyConnection.failSubmit = false;
        writes = pending.getContext().audio.writes;
        denied(pending.call("presave", null, request()));
        check(
                pending.getContext().audio.writes == writes,
                "pending submission rejects before audio");
        FreeKeyInputProvider startupFailure = provider();
        startupFailure.getContext().audio.failRead = 11;
        denied(startupFailure.call("bootstrap", null, request()));
        check(
                !startupFailure.getContext().preferences.contains("session"),
                "failed startup readback must not persist session");
        FreeKeyInputProvider failed = provider();
        FreeKeyConnection.failEnsure = true;
        denied(failed.call("bootstrap", null, request()));
        FreeKeyConnection.failEnsure = false;
        check(failed.getContext().audio.writes == 0, "ensure before startup zero");
        FreeKeyInputProvider deniedCaller = provider();
        android.os.Binder.uid = 1000;

        try {
            deniedCaller.call("bootstrap", null, request());
            throw new AssertionError("caller accepted");
        } catch (SecurityException expected) {
        } finally {
            android.os.Binder.uid = 2000;
        }

        check(deniedCaller.getContext().audio.writes == 0, "caller authorization before audio");
        System.out.println(
                "FreeStartupAudioProviderTest PASS: actual provider"
                        + " bootstrap/auth/presave/readback/direct receipt/lifecycle");
    }
}
