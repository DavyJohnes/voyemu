package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Exercise actual API30 Artifacts plan-before-apply using immutable normal producer request inputs.
 */
public final class MusicArtifactPipelineTest {
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

        return new Artifacts(
                new Artifacts.PatchedProfile(
                        profile, Json.object(Json.parse(Files.readString(data)))),
                storage,
                tools,
                root.resolve("profiles").resolve(profile.id()),
                work,
                path -> {
                    throw new AssertionError(
                            "Music must not acquire Android/native inputs: " + path);
                },
                log::add);
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 3, "ROOT ACTUAL_NORMAL_REQUEST PRIVATE_WORK");
        Path root = Path.of(args[0]).toRealPath(),
                requestFile = Path.of(args[1]).toRealPath(),
                work = Path.of(args[2]).toAbsolutePath().normalize();
        var request = Json.object(Json.parse(Files.readString(requestFile)));
        String id = Json.str(request, "profile");
        check(
                FilesSupport.hash(root.resolve("profiles").resolve(id).resolve("profile.json"))
                        .equals(Json.str(request, "profile_sha256")),
                "actual normal profile pin");
        var profile = new ProfileRegistry(root, id).get(id);
        var log = new ArrayList<String>();
        Path apk =
                producer(root, request, profile, work.resolve("valid"), log)
                        .resolve("patched:MusicConcate");
        var steps =
                Json.list(
                        Json.obj(Json.obj(profile.data(), "patches"), "MusicConcate").get("steps"));
        var expected = new ArrayList<String>();

        for (int pass = 0; pass < 2; pass++) {
            for (var step : steps) {
                expected.add("Applying patch MusicConcate: " + step);
            }
        }

        check(
                log.equals(expected),
                "actual Artifacts validates every raw step before applying the complete chain");
        Path original = root.resolve(".local/profiles/" + id + "/originals/MusicConcate.apk");
        var constants = TestProfiles.constants(root, MusicFullscreenGuard.NAME, original);
        var before = NativeCore.entries(original);
        var after = NativeCore.entries(apk);
        var entries = new TreeMap<>(before);
        entries.keySet().removeIf(DexOperations::signatureEntry);
        entries.remove("lib/armeabi-v7a/libBugly.so");
        entries.remove("lib/armeabi-v7a/libpl_droidsonroids_gif.so");
        entries.put(
                "AndroidManifest.xml",
                ManifestLibraries.add(
                        entries.get("AndroidManifest.xml"),
                        List.of("QGAPI", "QGMOD", "QGVehicle")));
        byte[] page = MusicPageGuard.apply(constants, 30, original, entries.get("classes.dex"));
        entries.put("classes.dex", MusicFullscreenGuard.apply(constants, 30, original, page));
        after.keySet().removeIf(DexOperations::signatureEntry);
        check(entries.keySet().equals(after.keySet()), "all actual producer APK entry inventory");

        for (var entry : entries.entrySet()) {
            check(
                    Arrays.equals(entry.getValue(), after.get(entry.getKey())),
                    "exact normal producer content: " + entry.getKey());
        }

        var reordered = Json.object(Json.parse(Json.write(profile.data())));
        var reorderedPatches = Json.obj(reordered, "patches");
        var reorderedMusic = Json.obj(reorderedPatches, "MusicConcate");
        var reorderedSteps = Json.list(reorderedMusic.get("steps"));
        Collections.swap(reorderedSteps, reorderedSteps.size() - 1, reorderedSteps.size() - 2);
        reorderedMusic.put("steps", reorderedSteps);
        reorderedPatches.put("MusicConcate", reorderedMusic);
        reordered.put("patches", reorderedPatches);
        check(
                Json.list(Json.obj(Json.obj(reordered, "patches"), "MusicConcate").get("steps"))
                        .get(reorderedSteps.size() - 2)
                        .equals("api30-music-fullscreen.json"),
                "negative fixture really places fullscreen before page guard");
        var invalid =
                new ProfileRegistry.Profile(
                        profile.id(),
                        profile.revision(),
                        reordered,
                        profile.resourcePrefix(),
                        profile.sourceRoot());
        boolean refused = false;

        try {
            producer(root, request, invalid, work.resolve("reordered"), new ArrayList<>())
                    .resolve("patched:MusicConcate");
        } catch (Failure failure) {
            refused = failure.code.equals("MUSIC_FULLSCREEN_CONTRACT");
        }

        check(
                refused && !Files.exists(work.resolve("reordered/patched-MusicConcate/signed.apk")),
                "reordered mutating chain still refuses raw DEX and produces no APK");
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
                                "reordered_chain_refused",
                                true,
                                "guest_started",
                                false,
                                "sdk_process_started",
                                false)));
    }
}
