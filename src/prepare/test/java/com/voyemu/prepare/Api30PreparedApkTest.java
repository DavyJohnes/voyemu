package com.voyemu.prepare;

import com.android.apksig.ApkVerifier;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedSet;
import java.util.TreeMap;
import java.util.TreeSet;

/** Actual producer output contract, not a synthetic readiness receipt. */
public final class Api30PreparedApkTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException("ROOT OWNED_ARTIFACT_WORK");
        }

        Path root = Path.of(args[0]).toRealPath(), work = Path.of(args[1]).toRealPath();
        // The qinggan11 profile that declares the original Navi graph.
        String id = TestProfiles.declaring(root, "original-navi-egl");
        var profile = new ProfileRegistry(root, id).get(id);
        var expectedApks =
                Set.of(
                        "Launcher",
                        "MusicConcate",
                        "Setting",
                        "SystemUI",
                        "SystemService",
                        "QGSettingProvider",
                        "KeyManagerService",
                        "Hiboard",
                        "VehicleAir",
                        "VehicleSetting",
                        "QGMedia",
                        "Navi",
                        "Camera",
                        "AudioPolicy");
        Map<String, String> originalPins = originalPins();
        Map<String, List<String>> expectedSteps = expectedSteps();
        Map<String, Map<String, String>> expectedInstall = expectedInstall();
        checkInstallGraph(profile, expectedApks, expectedInstall);
        Map<String, Object> checked = new TreeMap<>();

        for (var patch : Json.obj(profile.data(), "patches").entrySet()) {
            if (!"apk".equals(Json.object(patch.getValue()).get("type"))) {
                continue;
            }

            String name = patch.getKey();

            if (!expectedApks.contains(name)
                    || !patch.getValue()
                            .equals(
                                    Map.of(
                                            "type",
                                            "apk",
                                            "input",
                                            "original:" + name,
                                            "steps",
                                            expectedSteps.get(name)))) {
                throw new AssertionError("Unknown or changed APK patch chain: " + name);
            }

            checked.put(name, verifyPatchedApk(name, root, work, profile, originalPins));
        }

        checkQgapiConsumer(root, work, profile);

        if (!checked.keySet().equals(expectedApks)) {
            throw new AssertionError("Expected exact fourteen installed patched APKs");
        }

        System.out.println(
                Json.write(
                        Map.of(
                                "profile",
                                profile.id(),
                                "apks",
                                checked,
                                "guest_started",
                                false,
                                "machine_ready",
                                false)));
    }

    private static Map<String, String> originalPins() {
        Map<String, String> originalPins =
                Map.ofEntries(
                        Map.entry(
                                "AudioPolicy",
                                "2152a99acdbee41626c6b661fe6509dd2c9487d3cad6a66aad6320bbb00cc039"),
                        Map.entry(
                                "Launcher",
                                "b3287150827e7fa51072bcbcba257539101cda8433c5f9d3a95f4ca905abd567"),
                        Map.entry(
                                "MusicConcate",
                                "3dfa09d39638888de1f79d679867d1ac57c9a165a0eed6d43b39aedc4be1ea33"),
                        Map.entry(
                                "Setting",
                                "6f96bbeb58bf915b0a2d60817082cdca3523e3a2d00c144544cb92b4e5be6870"),
                        Map.entry(
                                "SystemUI",
                                "b676a3a426ba3b59fabb9d98355ff0e0ac7b07d7ad6545ac44d9cbf85f1700c1"),
                        Map.entry(
                                "SystemService",
                                "3b38818c46903a3786df70793712c2d2d043cfb516279b49fa412acf4a1e9c9c"),
                        Map.entry(
                                "QGSettingProvider",
                                "972c40dc151a9bc64ab29296176a8575d2521fed78e35c41b64ff5a6bf34c3f1"),
                        Map.entry(
                                "KeyManagerService",
                                "951abb4f86540663fd654138b7e58cd0226d5dfa0470a4f5e85384bcdedbb78e"),
                        Map.entry(
                                "Hiboard",
                                "902562587355d9e48d3878773da7955cd5a793c03e5ba6e5b595a317c7ec0f39"),
                        Map.entry(
                                "VehicleAir",
                                "b7b80ed1ff6b7b054d6035c6e39e7c04d97e9f2d906cd42f82f9da3a61e2fa2b"),
                        Map.entry(
                                "VehicleSetting",
                                "f37a53f1397678af472a87d89d903ba8f6ed6ac892d83ae3117b57bbe3b21360"),
                        Map.entry(
                                "QGMedia",
                                "533a0b0dc5ccbe91a42847fac20c5735bd8d78a28bbb980a6bf365dd5ed0fba0"),
                        Map.entry(
                                "Navi",
                                "dceddff70db38bacb50ebffa46d36da76f70e5596c6184bca51c48c3ed562a05"),
                        Map.entry(
                                "Camera",
                                "c680a80c0bfb13911979693c7dc46e8f75df69b361fc6f17a3a94aef28fbaabf"));

        return originalPins;
    }

    private static Map<String, List<String>> expectedSteps() {
        Map<String, List<String>> expectedSteps =
                Map.ofEntries(
                        Map.entry("AudioPolicy", List.of("api30-audio-policy.json")),
                        Map.entry(
                                "Launcher",
                                List.of(
                                        "framework-libraries.json",
                                        "api30-launcher-power.json",
                                        "api30-aqy-provider.json")),
                        Map.entry(
                                "MusicConcate",
                                List.of(
                                        "framework-libraries.json",
                                        "music-optional-arm32.json",
                                        "api30-music-page.json",
                                        "api30-music-fullscreen.json")),
                        Map.entry(
                                "Setting",
                                List.of(
                                        "framework-libraries.json",
                                        "api30-str-power.json",
                                        "api30-settings-voice-clone.json")),
                        Map.entry(
                                "SystemUI",
                                List.of("framework-libraries.json", "api30-systemui-routing.json")),
                        Map.entry(
                                "SystemService",
                                List.of("framework-libraries.json", "api30-network.json")),
                        Map.entry("QGSettingProvider", List.of("framework-libraries.json")),
                        Map.entry(
                                "KeyManagerService",
                                List.of("api30-keymanager.json", "api30-key-ingress.json")),
                        Map.entry(
                                "Hiboard",
                                List.of(
                                        "api30-hiboard-libraries.json",
                                        "api30-hiboard-routing.json",
                                        "api30-hiboard-startup.json")),
                        Map.entry("VehicleAir", List.of("api30-vehicle-libraries.json")),
                        Map.entry("VehicleSetting", List.of("api30-vehicle-libraries.json")),
                        Map.entry(
                                "QGMedia",
                                List.of(
                                        "api30-media-runtime.json",
                                        "api30-media-cloud-availability.json",
                                        "api30-media-notification-actions.json")),
                        Map.entry(
                                "Navi",
                                List.of(
                                        "api30-navigation-camera-libraries.json",
                                        "api30-navi-version.json")),
                        Map.entry("Camera", List.of("api30-navigation-camera-libraries.json")));

        return expectedSteps;
    }

    private static Map<String, Map<String, String>> expectedInstall() {
        Map<String, Map<String, String>> expectedInstall =
                Map.ofEntries(
                        Map.entry(
                                "AudioPolicy",
                                Map.of(
                                        "artifact",
                                        "patched:AudioPolicy",
                                        "type",
                                        "apk",
                                        "target",
                                        "/system/priv-app/AudioPolicy-release/AudioPolicy-release.apk",
                                        "package",
                                        "com.qinggan.audiopolicy.service")),
                        Map.entry(
                                "Launcher",
                                Map.of(
                                        "artifact",
                                        "patched:Launcher",
                                        "type",
                                        "apk",
                                        "target",
                                        "/system/priv-app/Launcher-release-signed/Launcher-release-signed.apk",
                                        "package",
                                        "com.qinggan.app.launcher")),
                        Map.entry(
                                "MusicConcate",
                                Map.of("artifact", "patched:MusicConcate", "type", "apk")),
                        Map.entry("Setting", Map.of("artifact", "patched:Setting", "type", "apk")),
                        Map.entry(
                                "SystemUI", Map.of("artifact", "patched:SystemUI", "type", "apk")),
                        Map.entry(
                                "SystemService",
                                Map.of("artifact", "patched:SystemService", "type", "apk")),
                        Map.entry(
                                "QGSettingProvider",
                                Map.of("artifact", "patched:QGSettingProvider", "type", "apk")),
                        Map.entry(
                                "KeyManagerService",
                                Map.of(
                                        "artifact",
                                        "patched:KeyManagerService",
                                        "type",
                                        "apk",
                                        "target",
                                        "/system/priv-app/KeyManagerService-release/KeyManagerService-release.apk",
                                        "package",
                                        "com.qinggan.keymanager.service")),
                        Map.entry(
                                "Hiboard",
                                Map.of(
                                        "artifact",
                                        "patched:Hiboard",
                                        "type",
                                        "apk",
                                        "target",
                                        "/system/priv-app/Hiboard-release-signed/Hiboard-release-signed.apk",
                                        "package",
                                        "com.qinggan.app.hiboard")),
                        Map.entry(
                                "VehicleAir",
                                Map.of("artifact", "patched:VehicleAir", "type", "apk")),
                        Map.entry(
                                "VehicleSetting",
                                Map.of("artifact", "patched:VehicleSetting", "type", "apk")),
                        Map.entry(
                                "QGMedia",
                                Map.of(
                                        "artifact",
                                        "patched:QGMedia",
                                        "type",
                                        "apk",
                                        "target",
                                        "/system/priv-app/qgmedia-release-signed/qgmedia-release-signed.apk",
                                        "package",
                                        "com.qinggan.media")),
                        Map.entry("Navi", Map.of("artifact", "patched:Navi", "type", "apk")),
                        Map.entry("Camera", Map.of("artifact", "patched:Camera", "type", "apk")));

        return expectedInstall;
    }

    private static void checkInstallGraph(
            ProfileRegistry.Profile profile,
            Set<String> expectedApks,
            Map<String, Map<String, String>> expectedInstall) {
        var installed = new TreeMap<String, Map<String, Object>>();

        for (Object value : Json.list(profile.data().get("install"))) {
            var entry = Json.object(value);
            String ref = Json.str(entry, "artifact");

            if (!ref.startsWith("patched:")) {
                continue;
            }

            String name = ref.substring(8);

            if (!expectedApks.contains(name)) {
                if ("apk".equals(entry.get("type"))) {
                    throw new AssertionError("Unknown installed patched APK: " + name);
                }

                continue;
            }

            if (!expectedInstall.get(name).equals(entry)
                    || installed.putIfAbsent(name, entry) != null) {
                throw new AssertionError("Wrong or duplicate patched APK installation: " + name);
            }
        }

        if (!installed.keySet().equals(expectedApks)) {
            throw new AssertionError("Exact fourteen patched APK install graph required");
        }
    }

    private static Map<String, Object> verifyPatchedApk(
            String name,
            Path root,
            Path work,
            ProfileRegistry.Profile profile,
            Map<String, String> originalPins)
            throws Exception {
        var original = Json.obj(profile.data(), "original");
        var spec = Json.object(original.get(name));
        Path input =
                root.resolve(
                        ".local/profiles/" + profile.id() + "/originals/" + Json.str(spec, "file"));

        checkOriginalPin(name, spec, input, originalPins);
        var before = new TreeMap<>(NativeCore.entries(input));
        Path output = work.resolve("patched-" + name + "/signed.apk");
        var after = new TreeMap<>(NativeCore.entries(output));
        Set<String> removed = removedNatives(name);

        for (String entry : removed) {
            if (!before.containsKey(entry) || after.containsKey(entry)) {
                throw new AssertionError("Exact optional native removal failed");
            }
        }

        if (name.equals("AudioPolicy")) {
            checkAudioPolicyHelper(before, after);
        }

        var expectedNames = expectedEntryNames(name, before, after, removed);
        int preserved = 0;
        long bytes = 0;

        for (String entry : expectedNames) {
            byte[] expected = rewrittenEntry(name, entry, profile, input, before.get(entry), after);

            if (expected == null) {
                expected = before.get(entry);
                preserved++;
                bytes += expected.length;
            }

            if (name.equals("Setting") && entry.equals("AndroidManifest.xml")) {
                expected =
                        SettingsManifest.apply(
                                Json.obj(
                                        Json.object(
                                                ProfileRegistry.patchMetadata(profile)
                                                        .get("api30-settings-voice-clone.json")),
                                        "constants"),
                                30,
                                input,
                                expected);
            }

            if (name.equals("QGMedia") && entry.equals("AndroidManifest.xml")) {
                expected =
                        MediaCloudManifestGuard.apply(
                                constants(profile, MediaCloudConfigGuard.NAME),
                                30,
                                "arm64-v8a",
                                input,
                                expected);
            }

            if (!Arrays.equals(expected, after.get(entry))) {
                throw new AssertionError("Unapproved content change: " + name + "/" + entry);
            }
        }

        checkSignature(name, output);

        if (!FilesSupport.hash(input).equals(Json.str(spec, "sha256"))) {
            throw new AssertionError("Original modified during verification");
        }

        return Map.of(
                "unchanged_entries",
                preserved,
                "unchanged_content_bytes",
                bytes,
                "removed_optional_native_entries",
                removed.size(),
                "signature_verified",
                true,
                "output_sha256",
                FilesSupport.hash(output));
    }

    private static void checkOriginalPin(
            String name, Map<String, Object> spec, Path input, Map<String, String> originalPins)
            throws Exception {
        if (!Json.str(spec, "file")
                        .equals(
                                name.equals("AudioPolicy")
                                        ? "AudioPolicy-release.apk"
                                        : name + ".apk")
                || !originalPins.get(name).equals(Json.str(spec, "sha256"))
                || !FilesSupport.hash(input).equals(originalPins.get(name))) {
            throw new AssertionError("Original pin changed: " + name);
        }
    }

    private static Set<String> removedNatives(String name) {
        Set<String> removed =
                name.equals("AudioPolicy")
                        ? Set.of("lib/armeabi/libqg_audio_jni.so")
                        : name.equals("QGMedia")
                                ? Set.of(
                                        "lib/armeabi-v7a/lib2496p2p.so",
                                        "lib/armeabi-v7a/libQPlayer.so",
                                        "lib/armeabi-v7a/libSuperSound3.so",
                                        "lib/armeabi-v7a/libcrypto_1_1.so",
                                        "lib/armeabi-v7a/libdeStream.so",
                                        "lib/armeabi-v7a/libkaolafmffmpeg.so",
                                        "lib/armeabi-v7a/libkaolafmplayer.so",
                                        "lib/armeabi-v7a/libkaolafmsdl.so",
                                        "lib/armeabi-v7a/libkaolafmse.so",
                                        "lib/armeabi-v7a/libkaolafmsoundtouch.so",
                                        "lib/armeabi-v7a/libkpk.so",
                                        "lib/armeabi-v7a/libkwbase.so",
                                        "lib/armeabi-v7a/libkwffmpeg.so",
                                        "lib/armeabi-v7a/libkwffutil.so",
                                        "lib/armeabi-v7a/libkwinfodecode.so",
                                        "lib/armeabi-v7a/libkwplayer.so",
                                        "lib/armeabi-v7a/libkwsdl.so",
                                        "lib/armeabi-v7a/libmusic3d.so",
                                        "lib/armeabi-v7a/libopenal.so",
                                        "lib/armeabi-v7a/libopenalwrapper.so",
                                        "lib/armeabi-v7a/libp2p.so",
                                        "lib/armeabi-v7a/libqcOpenSSL.so",
                                        "lib/armeabi-v7a/libssl_1_1.so",
                                        "lib/armeabi-v7a/libtmefriendship.so")
                                : name.equals("MusicConcate")
                                        ? Set.of(
                                                "lib/armeabi-v7a/libBugly.so",
                                                "lib/armeabi-v7a/libpl_droidsonroids_gif.so")
                                        : Set.of();

        return removed;
    }

    private static void checkAudioPolicyHelper(
            Map<String, byte[]> before, Map<String, byte[]> after) throws Exception {
        if (!NativeCore.hash(before.get("lib/armeabi/libqg_audio_jni.so"))
                        .equals("bae1f5b9867ac1ec8d9ea8f64f61bfca7bbdc5ecb28d4e14cfadeeb1ad32d27b")
                || before.containsKey("classes2.dex")
                || !after.containsKey("classes2.dex")) {
            throw new AssertionError(
                    "Exact provider JNI removal and collision-free helper DEX required");
        }

        var helper = AudioPolicyGuard.classes(after.get("classes2.dex"));

        if (helper.size() != 14
                || !helper.containsKey(AudioPolicyGuard.HELPER)
                || !helper.containsKey("Lcom/voyemu/oem/audio/OemAudioPolicyUnavailable;")
                || !Collections.disjoint(
                        helper.keySet(),
                        AudioPolicyGuard.classes(before.get("classes.dex")).keySet())) {
            throw new AssertionError("Exact fourteen helper classes must not shadow OEM classes");
        }

        for (String type : helper.keySet()) {
            if (!type.equals("Lcom/voyemu/oem/audio/OemAudioPolicyUnavailable;")
                    && !type.equals(AudioPolicyGuard.HELPER)
                    && !type.startsWith("Lcom/voyemu/oem/audio/OemAudioPolicy$")) {
                throw new AssertionError("Foreign helper class: " + type);
            }
        }
    }

    private static SortedSet<String> expectedEntryNames(
            String name, Map<String, byte[]> before, Map<String, byte[]> after, Set<String> removed)
            throws Exception {
        var expectedNames = new TreeSet<>(before.keySet());

        expectedNames.removeIf(DexOperations::signatureEntry);
        expectedNames.removeAll(removed);

        if (name.equals("AudioPolicy")) {
            expectedNames.add("classes2.dex");
        }

        if (name.equals("SystemUI")) {
            if (before.containsKey("classes2.dex")
                    || !after.containsKey("classes2.dex")
                    || !NativeCore.hash(after.get("classes2.dex"))
                            .equals(
                                    "9e457f6babc97106d00aef2ae11f7cd7e1a46804d1b90177aba6cf44287fa40b")) {
                throw new AssertionError("Exact own SystemUI routing helper DEX required");
            }

            expectedNames.add("classes2.dex");
        }

        if (name.equals("Hiboard")) {
            if (before.containsKey("classes3.dex")
                    || !after.containsKey("classes3.dex")
                    || !NativeCore.hash(after.get("classes3.dex"))
                            .equals(
                                    "001814c0475ff5ad28166c120f9d661cf33e50ae3c089a8054c355ba20c648d7")) {
                throw new AssertionError("Exact own Hiboard routing helper DEX required");
            }

            expectedNames.add("classes3.dex");
        }

        var actualNames = new TreeSet<>(after.keySet());
        actualNames.removeIf(DexOperations::signatureEntry);

        if (!expectedNames.equals(actualNames)) {
            throw new AssertionError("Unapproved APK entry inventory change: " + name);
        }

        return expectedNames;
    }

    private static void checkSignature(String name, Path output) throws Exception {
        var signature =
                new ApkVerifier.Builder(output.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .build()
                        .verify();

        if (!signature.isVerified()
                || signature.getSignerCertificates().size() != 1
                || !NativeCore.hash(signature.getSignerCertificates().get(0).getEncoded())
                        .equals(
                                "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8")) {
            throw new AssertionError("Prepared APK signature/base certificate differs: " + name);
        }
    }

    private static byte[] rewrittenEntry(
            String name,
            String entry,
            ProfileRegistry.Profile profile,
            Path input,
            byte[] expected,
            Map<String, byte[]> after)
            throws Exception {
        if ((name.equals("AudioPolicy") && entry.equals("classes2.dex"))
                || (name.equals("SystemUI") && entry.equals("classes2.dex"))
                || (name.equals("Hiboard") && entry.equals("classes3.dex"))) {
            return after.get(entry);
        }

        if (name.equals("SystemUI") && entry.equals("classes.dex")) {
            return SystemUiRouting.apply(
                    Json.obj(
                            Json.object(
                                    ProfileRegistry.patchMetadata(profile)
                                            .get("api30-systemui-routing.json")),
                            "constants"),
                    30,
                    input,
                    expected);
        }

        if (name.equals("AudioPolicy") && entry.equals("classes.dex")) {
            return AudioPolicyGuard.apply(
                    constants(profile, "api30-audio-policy"), 30, "arm64-v8a", input, expected);
        }

        if (entry.equals("AndroidManifest.xml") && !name.equals("AudioPolicy")) {
            return ManifestLibraries.add(
                    expected,
                    name.equals("KeyManagerService")
                            ? List.of("QGAPI", "QGMOD", "QGVehicle", "VoyemuFreeWheel")
                            : List.of("QGAPI", "QGMOD", "QGVehicle"));
        }

        return rewrittenGuardEntry(name, entry, profile, input, expected);
    }

    private static byte[] rewrittenGuardEntry(
            String name, String entry, ProfileRegistry.Profile profile, Path input, byte[] expected)
            throws Exception {
        if (name.equals("Launcher")
                && (entry.equals("classes.dex") || entry.equals("classes2.dex"))) {
            expected =
                    LauncherPowerGuard.apply(
                            constants(profile, LauncherPowerGuard.NAME),
                            30,
                            input,
                            expected,
                            entry);

            if (entry.equals("classes2.dex")) {
                expected =
                        AqyProviderGuard.apply(
                                constants(profile, AqyProviderGuard.NAME), 30, input, expected);
            }

            return expected;
        }

        if (name.equals("KeyManagerService") && entry.equals("classes.dex")) {
            expected =
                    KeyManagerGuard.apply(
                            constants(profile, KeyManagerGuard.NAME), 30, input, expected);
            expected =
                    KeyManagerIngressGuard.apply(
                            constants(profile, KeyManagerIngressGuard.NAME), 30, input, expected);

            return expected;
        }

        if (name.equals("Hiboard") && entry.equals("classes.dex")) {
            var constants =
                    Json.obj(
                            Json.object(
                                    ProfileRegistry.patchMetadata(profile)
                                            .get("api30-hiboard-startup.json")),
                            "constants");

            return HiboardStartupGuard.apply(
                    constants, 30, input, HiboardRouting.apply(constants, 30, input, expected));
        }

        if (name.equals("QGMedia") && entry.equals("classes2.dex")) {
            return MediaNotificationGuard.apply(
                    constants(profile, MediaNotificationGuard.NAME),
                    30,
                    "arm64-v8a",
                    input,
                    expected);
        }

        if (name.equals("Navi") && entry.equals("classes3.dex")) {
            return NaviVersionGuard.apply(TestProfiles.SYNTHETIC, 30, "arm64-v8a", input, expected);
        }

        return rewrittenServiceEntry(name, entry, profile, input, expected);
    }

    private static byte[] rewrittenServiceEntry(
            String name, String entry, ProfileRegistry.Profile profile, Path input, byte[] expected)
            throws Exception {
        if (name.equals("QGMedia") && entry.equals("classes3.dex")) {
            return MediaCloudConfigGuard.apply(
                    constants(profile, MediaCloudConfigGuard.NAME),
                    30,
                    "arm64-v8a",
                    input,
                    expected);
        }

        if (name.equals("MusicConcate") && entry.equals("classes.dex")) {
            return MusicFullscreenGuard.apply(
                    constants(profile, MusicFullscreenGuard.NAME),
                    30,
                    input,
                    MusicPageGuard.apply(
                            constants(profile, MusicFullscreenGuard.NAME), 30, input, expected));
        }

        if (name.equals("SystemService") && entry.equals("classes.dex")) {
            return NetworkGuard.apply(constants(profile, NetworkGuard.NAME), 30, input, expected);
        }

        if (name.equals("Setting") && entry.equals("classes.dex")) {
            return SoundBalanceGuard.apply(
                    30,
                    SettingsVoiceCloneGuard.apply(
                            constants(profile, SettingsVoiceCloneGuard.NAME), 30, input, expected));
        }

        if (name.equals("Setting") && entry.equals("classes2.dex")) {
            return StrPowerGuard.apply(constants(profile, StrPowerGuard.NAME), 30, input, expected);
        }

        return null;
    }

    /** The constants of the step NAME.json that the profile declares. */
    static Map<String, Object> constants(ProfileRegistry.Profile profile, String name)
            throws Exception {
        return Json.obj(
                Json.object(ProfileRegistry.patchMetadata(profile).get(name + ".json")),
                "constants");
    }

    private static void checkQgapiConsumer(Path root, Path work, ProfileRegistry.Profile profile)
            throws Exception {
        if (!Json.obj(profile.data(), "patches")
                .get("QGAPI")
                .equals(
                        Map.of(
                                "type",
                                "jar",
                                "input",
                                "original:QGAPI",
                                "steps",
                                List.of("api30-audio-consumer.json")))) {
            throw new AssertionError("Exact QGAPI consumer chain required");
        }

        long qgapiInstalls =
                Json.list(profile.data().get("install")).stream()
                        .map(Json::object)
                        .filter(e ->
                                "/system/framework/QGAPI.jar".equals(e.get("target"))
                                        && "patched:QGAPI".equals(e.get("artifact"))
                                        && "file".equals(e.get("type")))
                        .count();

        if (qgapiInstalls != 1
                || Json.list(profile.data().get("install")).stream()
                        .map(Json::object)
                        .anyMatch(e -> "original:QGAPI".equals(e.get("artifact")))) {
            throw new AssertionError("Install the genuine guarded QGAPI exactly once");
        }

        var qgapiSpec = Json.obj(Json.obj(profile.data(), "original"), "QGAPI");
        Path qgapiInput =
                root.resolve(
                        ".local/profiles/"
                                + profile.id()
                                + "/originals/"
                                + Json.str(qgapiSpec, "file"));

        if (!Json.str(qgapiSpec, "file").equals("framework/QGAPI.jar")
                || !FilesSupport.hash(qgapiInput).equals(Json.str(qgapiSpec, "sha256"))) {
            throw new AssertionError("Exact immutable QGAPI source required");
        }

        var qgapiBefore = NativeCore.entries(qgapiInput);
        var qgapiAfter = NativeCore.entries(work.resolve("patched-QGAPI/patched.jar"));

        if (!qgapiBefore.keySet().equals(qgapiAfter.keySet())) {
            throw new AssertionError("QGAPI whole ZIP inventory changed");
        }

        for (var e : qgapiBefore.entrySet()) {
            byte[] expected =
                    e.getKey().equals("classes.dex")
                            ? AudioPolicyConsumerGuard.apply(
                                    constants(profile, "api30-audio-consumer"),
                                    30,
                                    "arm64-v8a",
                                    qgapiInput,
                                    e.getValue())
                            : e.getValue();

            if (!Arrays.equals(expected, qgapiAfter.get(e.getKey()))) {
                throw new AssertionError("Unapproved QGAPI entry mutation: " + e.getKey());
            }
        }

        if (!FilesSupport.hash(qgapiInput).equals(Json.str(qgapiSpec, "sha256"))) {
            throw new AssertionError("QGAPI original modified");
        }
    }
}
