package com.qinggan.audiopolicy;

import android.content.Context;
import android.os.Binder;
import android.os.Bundle;

import org.json.JSONObject;

import java.nio.file.Files;
import java.nio.file.Path;

public final class AudioStartupProviderTest {
    private static int checks;
    private static final String BOOT = "12345678-1234-1234-1234-123456789abc";
    private static final String SESSION = "1123456789abcdef0123456789abcdef";

    private static void need(boolean condition, String message) {
        checks++;

        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void denied(Runnable action) {
        try {
            action.run();
            throw new AssertionError("request accepted");
        } catch (SecurityException | IllegalArgumentException | IllegalStateException expected) {
            checks++;
        }
    }

    private static final class Provider extends AudioStartupProvider {
        final StartupAudioPolicyTest.Owner owner;

        Provider(StartupAudioPolicyTest.Owner owner) {
            this.owner = owner;
        }

        public Context getContext() {
            return owner;
        }

        protected String currentBoot() {
            return BOOT;
        }
    }

    private static Bundle request(String session, String boot, long generation) {
        Bundle args = new Bundle();

        args.putString("launch_session", session);
        args.putString("boot_id", boot);
        args.putLong("android_generation", generation);

        return args;
    }

    private static JSONObject call(Provider provider, String method, Bundle args) {
        Bundle reply = provider.call(method, null, args);
        String json = reply.getString("json");

        need(
                json != null
                        && json.contains("\"stream_readbacks\":[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]"),
                "exact eleven readbacks");

        return new JSONObject(json);
    }

    public static void main(String[] args) throws Exception {
        String boot = BOOT;
        StartupAudioPolicyTest.Owner owner = new StartupAudioPolicyTest.Owner();
        String manifest =
                Files.readString(Path.of(args[0]).resolve("audiopolicy/AndroidManifest.xml"));

        owner.audio.phoneGranted = manifest.contains("android.permission.MODIFY_PHONE_STATE");
        owner.audio.accessibilityGranted =
                manifest.contains("android.permission.CHANGE_ACCESSIBILITY_VOLUME");
        // Pre-existing zeros would hide AudioService's silent permission denials.
        owner.audio.values[0] = 4;
        owner.audio.values[10] = 7;
        Provider provider = new Provider(owner);
        need(provider.onCreate(), "provider creation read-only");
        Bundle valid = request(SESSION, boot, 1);
        denied(() -> provider.call("status", null, valid));
        Binder.uid = 10001;
        denied(() -> provider.call("reset_launch", null, valid));
        Binder.uid = 2000;
        owner.permission = -1;
        denied(() -> provider.call("reset_launch", null, valid));
        owner.permission = 0;
        denied(() -> provider.call("reset_launch", null, null));
        Bundle missing = request(SESSION, boot, 1);
        missing.remove("boot_id");
        denied(() -> provider.call("reset_launch", null, missing));
        Bundle unknown = request(SESSION, boot, 1);
        unknown.putString("extra", "unexpected");
        denied(() -> provider.call("reset_launch", null, unknown));
        denied(() -> provider.call("reset_launch", null, request("wrong", boot, 1)));
        denied(() ->
                provider.call(
                        "reset_launch",
                        null,
                        request(SESSION, "22345678-1234-1234-1234-123456789abc", 1)));
        denied(() -> provider.call("reset_launch", null, request(SESSION, boot, 0)));
        Bundle badType = request(SESSION, boot, 1);
        badType.putInt("android_generation", 1);
        denied(() -> provider.call("reset_launch", null, badType));
        denied(() -> provider.call("unknown", null, valid));
        denied(() -> provider.call("reset_launch", "argument", valid));
        JSONObject initial = call(provider, "reset_launch", valid);
        need(
                Boolean.TRUE.equals(initial.get("all_zero"))
                        && Boolean.TRUE.equals(initial.get("oem25_zero"))
                        && Boolean.TRUE.equals(initial.get("completed")),
                "new launch acknowledgement");
        need(
                SESSION.equals(initial.get("launch_session"))
                        && boot.equals(initial.get("boot_id")),
                "bound identity reply");
        owner.volumes.values.put("stream-4", 15);
        String oemOnly = provider.call("status", null, valid).getString("json");
        need(
                oemOnly.contains("\"all_zero\":false") && oemOnly.contains("\"oem25_zero\":false"),
                "all_zero includes inactive OEM stream");
        provider.call("prepare_snapshot", null, valid);
        owner.audio.values[3] = 5;
        owner.volumes.values.put("stream-3", 15);
        int before = owner.audio.writes, commits = owner.identity.commits;
        String state = provider.call("status", null, valid).getString("json");
        need(
                state.contains("\"all_zero\":false") && state.contains("\"oem25_zero\":false"),
                "status reflects user choice");
        provider.call("reset_launch", null, valid);
        Provider restarted = new Provider(owner);
        restarted.call("reset_launch", null, valid);
        android.os.SystemProperties.token = "0123456789abcdef0123456789abcdef";
        StartupAudioPolicy.ensureBoot(owner, boot);
        need(
                owner.audio.values[3] == 5
                        && Integer.valueOf(15).equals(owner.volumes.values.get("stream-3")),
                "same generation and stale qemu token preserve user choice");
        need(
                owner.audio.writes == before && owner.identity.commits == commits,
                "read-only status and duplicate generation");
        denied(() ->
                provider.call(
                        "status", null, request("2123456789abcdef0123456789abcdef", boot, 1)));
        denied(() -> provider.call("prepare_snapshot", null, request(SESSION, boot, 2)));
        JSONObject saved = call(provider, "prepare_snapshot", valid);
        need(
                Boolean.TRUE.equals(saved.get("all_zero"))
                        && Boolean.TRUE.equals(saved.get("oem25_zero")),
                "pre-snapshot forces zero");
        owner.audio.values[4] = 7;
        owner.volumes.values.put("stream-4", 14);
        call(provider, "reset_launch", request(SESSION, boot, 2));
        need(
                owner.audio.values[4] == 0
                        && Integer.valueOf(0).equals(owner.volumes.values.get("stream-4")),
                "new Android generation");
        denied(() -> provider.call("reset_launch", null, valid));
        call(provider, "reset_launch", request("2123456789abcdef0123456789abcdef", boot, 1));
        owner.audio.values[7] = 4;
        owner.audio.reject = 7;
        denied(() ->
                provider.call(
                        "prepare_snapshot",
                        null,
                        request("2123456789abcdef0123456789abcdef", boot, 1)));
        owner.audio.reject = -1;
        StartupAudioPolicyTest.Owner failing = new StartupAudioPolicyTest.Owner();
        Provider retry = new Provider(failing);
        failing.identity.writable = false;
        denied(() -> retry.call("reset_launch", null, valid));
        denied(() -> retry.call("status", null, valid));
        failing.identity.writable = true;
        call(retry, "reset_launch", valid);
        need(
                failing.identity.persisted.containsKey("completed"),
                "retry persists actual checkpoint");
        Binder.uid = 0;
        call(retry, "status", valid);
        Binder.uid = 2000;
        StartupAudioPolicyTest.Owner permission = new StartupAudioPolicyTest.Owner();
        Provider guarded = new Provider(permission);
        permission.audio.values[0] = 4;
        permission.audio.values[10] = 7;
        permission.audio.phoneGranted = false;
        permission.audio.accessibilityGranted = false;
        denied(() -> guarded.call("reset_launch", null, valid));
        need(
                permission.audio.values[0] == 4
                        && permission.audio.values[10] == 7
                        && !permission.identity.persisted.containsKey("completed"),
                "silent voice and accessibility refusals cannot acknowledge");
        permission.audio.phoneGranted = true;
        denied(() -> guarded.call("reset_launch", null, valid));
        need(
                permission.audio.values[0] == 0
                        && permission.audio.values[10] == 7
                        && !permission.identity.persisted.containsKey("completed"),
                "accessibility permission is independently required");
        permission.audio.accessibilityGranted = true;
        call(guarded, "reset_launch", valid);
        need(
                permission.audio.values[0] == 0
                        && permission.audio.values[10] == 0
                        && permission.identity.persisted.containsKey("completed"),
                "both signature grants reset nonzero protected streams");
        need(
                Binder.clears == Binder.restores && Binder.clears > 0,
                "identity restored after success and failure");
        System.out.println("PASS A9 AudioStartupProvider: " + checks + " checks");
    }
}
