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

/**
 * Actual Artifacts adapter preflight/apply/package regression, without guest or whole-profile
 * build.
 */
public final class HiboardArtifactPipelineTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]).toRealPath(),
                requestPath = Path.of(a[1]).toRealPath(),
                job = Path.of(a[2]).toRealPath();
        boolean red = a.length == 4 && a[3].equals("--red");
        var req = Json.object(Json.parse(Files.readString(requestPath)));
        var selected = Json.obj(req, "tools");
        var sign = Json.obj(selected, "signing");
        var platform = Json.obj(selected, "platform");
        Path manifest = root.resolve("src/app/toolchain.json");

        checkToolPins(manifest, selected, sign, platform);
        // The profile whose Hiboard original carries the routing helper pinned below.
        String id =
                TestProfiles.pinning(
                        root, "902562587355d9e48d3878773da7955cd5a793c03e5ba6e5b595a317c7ec0f39");
        var base = new ProfileRegistry(root, id).get(id);
        var data = Json.object(Json.parse(Json.write(base.data())));
        var defs = Json.obj(data, "patches");
        var hiboard = Json.obj(defs, "Hiboard");
        var steps =
                List.of(
                        "api30-hiboard-libraries.json",
                        "api30-hiboard-routing.json",
                        "api30-hiboard-startup.json");
        hiboard.put("steps", steps);
        defs.put("Hiboard", hiboard);
        data.put("patches", defs);
        var profile =
                new ProfileRegistry.Profile(
                        id, base.revision(), data, base.resourcePrefix(), base.sourceRoot());
        var patchData = new LinkedHashMap<String, Object>(ProfileRegistry.patchMetadata(base));
        Path recipe = root.resolve("profiles/" + id + "/patches/api30-hiboard-startup.json");
        var startup = Json.object(Json.parse(Files.readString(recipe)));
        startup.put("sourceSha256", FilesSupport.hash(recipe));
        patchData.put(steps.get(2), startup);
        Path work = job.resolve(red ? "artifact-red" : "artifact-green");
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
                        path -> {
                            throw new AssertionError("No Android read permitted: " + path);
                        },
                        log::add);

        if (red) {
            boolean rejected = false;

            try {
                artifacts.resolve("patched:Hiboard");
            } catch (Failure e) {
                rejected = e.code.equals("HIBOARD_STARTUP");
            }

            check(
                    rejected && !Files.exists(work.resolve("patched-Hiboard/signed.apk")),
                    "old unconditional apply adapter fails pass0 before APK");
            System.out.println(
                    "RED actual Artifacts pass0 rejects clean DEX under unconditional apply"
                            + " adapter");

            return;
        }

        Path apk = artifacts.resolve("patched:Hiboard");
        var expectedLog = new ArrayList<String>();

        for (int pass = 0; pass < 2; pass++) {
            for (var step : steps) {
                expectedLog.add("Applying patch Hiboard: " + step);
            }
        }

        check(log.equals(expectedLog), "actual full raw preflight then ordered apply");
        Path original = root.resolve(".local/profiles/" + id + "/originals/Hiboard.apk");
        var expected = new TreeMap<>(NativeCore.entries(original));
        expected.keySet().removeIf(DexOperations::signatureEntry);
        expected.put(
                "AndroidManifest.xml",
                ManifestLibraries.add(
                        expected.get("AndroidManifest.xml"),
                        List.of("QGAPI", "QGMOD", "QGVehicle")));
        expected.put(
                "classes.dex",
                HiboardStartupGuard.apply(
                        Json.obj(startup, "constants"),
                        30,
                        original,
                        HiboardRouting.apply(
                                Json.obj(startup, "constants"),
                                30,
                                original,
                                expected.get("classes.dex"))));
        var after = new TreeMap<>(NativeCore.entries(apk));
        after.keySet().removeIf(DexOperations::signatureEntry);
        check(
                after.containsKey("classes3.dex")
                        && NativeCore.hash(after.get("classes3.dex"))
                                .equals(
                                        "001814c0475ff5ad28166c120f9d661cf33e50ae3c089a8054c355ba20c648d7"),
                "existing routing helper unchanged");
        expected.put("classes3.dex", after.get("classes3.dex"));
        check(expected.keySet().equals(after.keySet()), "exact APK entry inventory");

        for (var e : expected.entrySet()) {
            check(
                    Arrays.equals(e.getValue(), after.get(e.getKey())),
                    "exact content " + e.getKey());
        }

        check(
                NativeCore.hash(after.get("classes.dex"))
                        .equals("e57744c0fa72be08541d469a37d5373283648133359e2c3faaa52f4a387db6d6"),
                "guarded output DEX");
        checkPlatformSignature(apk);
        String receipt =
                Json.write(
                        Map.of(
                                "actual_Artifacts_pass0_pass1",
                                true,
                                "ordered_steps",
                                steps,
                                "all_entries_verified",
                                true,
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

    static void checkToolPins(
            Path manifest,
            Map<String, Object> selected,
            Map<String, Object> sign,
            Map<String, Object> platform)
            throws Exception {
        check(
                FilesSupport.hash(manifest).equals(Json.str(selected, "manifest_sha256")),
                "manifest pin");

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
    }

    static void checkPlatformSignature(Path apk) throws Exception {
        var verified =
                new ApkVerifier.Builder(apk.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .build()
                        .verify();

        check(
                verified.isVerified()
                        && verified.getSignerCertificates().size() == 1
                        && NativeCore.hash(verified.getSignerCertificates().get(0).getEncoded())
                                .equals(
                                        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8"),
                "normal platform signature");
    }
}
