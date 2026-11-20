package com.voyemu.prepare;

import com.android.apksig.ApkVerifier;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class SettingsVoiceCloneArtifactTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]).toRealPath(),
                request = Path.of(a[1]).toRealPath(),
                job = Path.of(a[2]).toRealPath();
        var selected = Json.obj(Json.object(Json.parse(Files.readString(request))), "tools");
        var sign = Json.obj(selected, "signing");
        var platform = Json.obj(selected, "platform");
        Path manifest = root.resolve("src/app/toolchain.json");

        check(
                FilesSupport.hash(manifest).equals(Json.str(selected, "manifest_sha256")),
                "toolchain manifest");

        for (var pair :
                List.of(
                        Map.entry("key", "key_sha256"),
                        Map.entry("certificate", "certificate_sha256"))) {
            check(
                    FilesSupport.hash(Path.of(Json.str(sign, pair.getKey())))
                            .equals(Json.str(sign, pair.getValue())),
                    "sign input pin");
        }

        check(
                FilesSupport.hash(Path.of(Json.str(platform, "file")))
                        .equals(Json.str(platform, "sha256")),
                "platform pin");
        String id = "free-11-chinese-20260731-7.3.1";
        var base = new ProfileRegistry(root, id).get(id);
        var data =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        job.resolve("overlay/profiles/" + id + "/profile.json"))));
        var profile =
                new ProfileRegistry.Profile(
                        id, base.revision(), data, base.resourcePrefix(), base.sourceRoot());
        var patchData = new LinkedHashMap<String, Object>(ProfileRegistry.patchMetadata(base));
        Path recipe =
                root.resolve(
                        "profiles/" + id + "/patches/" + SettingsVoiceCloneGuard.NAME + ".json");
        var metadata = Json.object(Json.parse(Files.readString(recipe)));
        metadata.put("sourceSha256", FilesSupport.hash(recipe));
        patchData.put(SettingsVoiceCloneGuard.NAME + ".json", metadata);
        var steps =
                List.of(
                        "framework-libraries.json",
                        "api30-str-power.json",
                        SettingsVoiceCloneGuard.NAME + ".json");
        check(
                Json.obj(Json.obj(data, "patches"), "Setting").get("steps").equals(steps),
                "exact3step chain");
        Path work = job.resolve("artifact-green");
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        var tools =
                new Toolchains(
                        storage,
                        Json.object(Json.parse(Files.readString(manifest))),
                        Path.of(Json.str(selected, "sdk")),
                        Path.of(Json.str(sign, "key")),
                        Path.of(Json.str(sign, "certificate")),
                        Path.of(Json.str(platform, "file")));
        var log = new ArrayList<String>();
        var artifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, patchData),
                        storage,
                        tools,
                        root.resolve("profiles/" + id),
                        work,
                        p -> {
                            throw new AssertionError("No Android read " + p);
                        },
                        log::add);
        Path apk = artifacts.resolve("patched:Setting");
        var expectedLog = new ArrayList<String>();

        for (int pass = 0; pass < 2; pass++) {
            for (var step : steps) {
                expectedLog.add("Applying patch Setting: " + step);
            }
        }

        check(log.equals(expectedLog), "pass0/pass1 ordered chain");
        Path original = root.resolve(".local/profiles/" + id + "/originals/Setting.apk");
        var before = new TreeMap<>(NativeCore.entries(original));
        before.keySet().removeIf(DexOperations::signatureEntry);
        before.put(
                "AndroidManifest.xml",
                SettingsManifest.apply(
                        id,
                        30,
                        original,
                        ManifestLibraries.add(
                                before.get("AndroidManifest.xml"),
                                List.of("QGAPI", "QGMOD", "QGVehicle"))));
        before.put(
                "classes2.dex",
                StrPowerGuard.apply(
                        Json.obj(Json.object(patchData.get("api30-str-power.json")), "constants"),
                        30,
                        original,
                        before.get("classes2.dex")));
        before.put(
                "classes.dex",
                SettingsVoiceCloneGuard.apply(
                        Json.obj(metadata, "constants"), 30, original, before.get("classes.dex")));
        var after = new TreeMap<>(NativeCore.entries(apk));
        after.keySet().removeIf(DexOperations::signatureEntry);
        check(before.keySet().equals(after.keySet()), "APK entries inventory");

        for (var e : before.entrySet()) {
            check(
                    Arrays.equals(e.getValue(), after.get(e.getKey())),
                    "exact APK entry " + e.getKey());
        }

        var signature =
                new ApkVerifier.Builder(apk.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .build()
                        .verify();
        check(
                signature.isVerified()
                        && signature.getSignerCertificates().size() == 1
                        && NativeCore.hash(signature.getSignerCertificates().get(0).getEncoded())
                                .equals(
                                        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8"),
                "platform signature");
        String receipt =
                Json.write(
                        Map.of(
                                "actual_Artifacts_pass0_pass1",
                                true,
                                "all_entries_verified",
                                true,
                                "steps",
                                steps,
                                "signed_apk",
                                apk.toString(),
                                "signed_sha256",
                                FilesSupport.hash(apk),
                                "dex_sha256",
                                NativeCore.hash(after.get("classes.dex")),
                                "guest_started",
                                false));
        Files.writeString(job.resolve("artifact-pipeline-receipt.json"), receipt + "\n");
        System.out.println(receipt);
    }
}
