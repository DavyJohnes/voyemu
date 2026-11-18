package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Exercise actual API30 Artifacts plan-before-apply using immutable normal producer request inputs.
 */
public final class MediaCloudArtifactPipelineTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Artifacts producer(
            Path root,
            Map<String, Object> request,
            ProfileRegistry.Profile profile,
            Path work,
            List<String> log)
            throws Exception {
        var selected = Json.obj(request, "tools");
        var signing = Json.obj(selected, "signing");
        var platform = Json.obj(selected, "platform");

        for (var pair :
                List.of(
                        Map.entry("key", "key_sha256"),
                        Map.entry("certificate", "certificate_sha256"))) {
            check(
                    FilesSupport.hash(Path.of(Json.str(signing, pair.getKey())))
                            .equals(Json.str(signing, pair.getValue())),
                    "actual normal signing input pin");
        }

        check(
                FilesSupport.hash(Path.of(Json.str(platform, "file")))
                        .equals(Json.str(platform, "sha256")),
                "actual platform pin");
        Path manifest = root.resolve("src/app/toolchain.json");
        check(
                FilesSupport.hash(manifest).equals(Json.str(selected, "manifest_sha256")),
                "actual tool manifest pin");
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
                        Path.of(Json.str(signing, "key")),
                        Path.of(Json.str(signing, "certificate")),
                        Path.of(Json.str(platform, "file")));
        Path data = Path.of(Json.str(request, "patch_data"));
        check(
                FilesSupport.hash(data).equals(Json.str(request, "patch_data_sha256")),
                "actual normal recipe transport pin");
        var metadata = Json.object(Json.parse(Files.readString(data)));
        metadata.put(
                MediaCloudConfigGuard.NAME + ".json",
                Map.of(
                        "schema",
                        1,
                        "api",
                        30,
                        "sourceSha256",
                        FilesSupport.hash(
                                root.resolve(
                                        "profiles/free-11-chinese-20260731-7.3.1/patches/api30-media-cloud-availability.json")),
                        "constants",
                        Map.of(
                                "original_sha256_allowlist",
                                List.of(MediaCloudConfigGuard.pins(PROFILE).get(0))),
                        "calls",
                        List.of(
                                Map.of(
                                        "operation",
                                        "guardMediaCloudAvailability",
                                        "args",
                                        List.of()))));

        return new Artifacts(
                new Artifacts.PatchedProfile(profile, metadata),
                storage,
                tools,
                root.resolve("profiles").resolve(profile.id()),
                work,
                path -> {
                    throw new AssertionError(
                            "Media must not acquire Android/native inputs: " + path);
                },
                log::add);
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 3, "ROOT ACTUAL_NORMAL_REQUEST PRIVATE_WORK");
        Path root = Path.of(args[0]).toRealPath(),
                requestFile = Path.of(args[1]).toRealPath(),
                work = Path.of(args[2]).toAbsolutePath().normalize();
        var request = Json.object(Json.parse(Files.readString(requestFile)));
        String id = PROFILE;
        check(
                Json.str(request, "profile").equals(id)
                        && FilesSupport.hash(
                                        root.resolve("profiles")
                                                .resolve(id)
                                                .resolve("profile.json"))
                                .equals(Json.str(request, "profile_sha256")),
                "actual normal profile pin");
        var actualProfile = new ProfileRegistry(root, id).get(id);
        var data = Json.object(Json.parse(Json.write(actualProfile.data())));
        var patches = Json.obj(data, "patches");
        var patch = Json.obj(patches, "QGMedia");
        patch.put(
                "steps", List.of("api30-media-runtime.json", MediaCloudConfigGuard.NAME + ".json"));
        patches.put("QGMedia", patch);
        data.put("patches", patches);
        var profile =
                new ProfileRegistry.Profile(
                        actualProfile.id(),
                        actualProfile.revision(),
                        data,
                        actualProfile.resourcePrefix(),
                        actualProfile.sourceRoot());
        var log = new ArrayList<String>();
        Path apk =
                producer(root, request, profile, work.resolve("valid"), log)
                        .resolve("patched:QGMedia");
        var steps =
                Json.list(Json.obj(Json.obj(profile.data(), "patches"), "QGMedia").get("steps"));
        var expected = new ArrayList<String>();

        for (int pass = 0; pass < 2; pass++) {
            for (var step : steps) {
                expected.add("Applying patch QGMedia: " + step);
            }
        }

        check(
                log.equals(expected),
                "actual Artifacts validates every raw step before applying the complete chain");
        Path original = root.resolve(".local/profiles/" + id + "/originals/QGMedia.apk");
        var before = NativeCore.entries(original);
        var after = NativeCore.entries(apk);
        var entries = new TreeMap<>(before);
        entries.keySet().removeIf(DexOperations::signatureEntry);
        entries.keySet().removeIf(n -> n.startsWith("lib/armeabi-v7a/"));
        entries.put(
                "AndroidManifest.xml",
                ManifestLibraries.add(
                        entries.get("AndroidManifest.xml"),
                        List.of("QGAPI", "QGMOD", "QGVehicle")));
        entries.put(
                "AndroidManifest.xml",
                MediaCloudManifestGuard.apply(
                        id, 30, "arm64-v8a", original, entries.get("AndroidManifest.xml")));
        entries.put(
                "classes3.dex",
                MediaCloudConfigGuard.apply(
                        id, 30, "arm64-v8a", original, entries.get("classes3.dex")));
        after.keySet().removeIf(DexOperations::signatureEntry);
        check(entries.keySet().equals(after.keySet()), "all actual producer APK entry inventory");

        for (var entry : entries.entrySet()) {
            check(
                    Arrays.equals(entry.getValue(), after.get(entry.getKey())),
                    "exact normal producer content: " + entry.getKey());
        }

        var signature =
                new com.android.apksig.ApkVerifier.Builder(apk.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .build()
                        .verify();
        check(
                signature.isVerified()
                        && signature.getSignerCertificates().size() == 1
                        && NativeCore.hash(signature.getSignerCertificates().get(0).getEncoded())
                                .equals(
                                        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8"),
                "actual platform signature");
        System.out.println(
                Json.write(
                        Map.of(
                                "actual_request",
                                requestFile.toString(),
                                "actual_profile_sha256",
                                Json.str(request, "profile_sha256"),
                                "signed_apk",
                                apk.toString(),
                                "signed_apk_sha256",
                                FilesSupport.hash(apk),
                                "raw_plan_then_apply",
                                true,
                                "all_apk_entries_verified",
                                true,
                                "platform_signature_verified",
                                true,
                                "guest_started",
                                false,
                                "sdk_process_started",
                                false)));
    }
}
