package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.ZipFile;

/** Actual Artifacts two-pass recipe integration against the selected production profile. */
public final class PassionBluetoothVolumeArtifactsTest {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static Map<String, byte[]> entries(Path path) throws Exception {
        var out = new TreeMap<String, byte[]>();

        try (var zip = new ZipFile(path.toFile())) {
            for (var e : Collections.list(zip.entries())) {
                if (!e.isDirectory()) {
                    out.put(e.getName(), zip.getInputStream(e).readAllBytes());
                }
            }
        }

        return out;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]), expected = Path.of(args[2]);

        Files.createDirectory(stage);
        var request = Json.object(Json.parse(Files.readString(Path.of(args[3]))));
        var registry = new ProfileRegistry(root, PassionServiceForegroundGuard.PROFILE);
        var profile = registry.get(PassionServiceForegroundGuard.PROFILE);
        Path local = root.resolve(".local");
        var storage =
                new Storage(
                        local,
                        local.resolve("config"),
                        local.resolve("cache"),
                        local.resolve("state"),
                        local.resolve("run"),
                        local.resolve("logs"));
        check(Inputs.check(profile, storage).ok());
        var tools = Json.obj(request, "tools");
        var signing = Json.obj(tools, "signing");
        var toolchain =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var selected =
                new Toolchains(
                        storage,
                        toolchain,
                        Path.of(Json.str(tools, "sdk")),
                        Path.of(Json.str(signing, "key")),
                        Path.of(Json.str(signing, "certificate")),
                        Path.of(Json.str(Json.obj(tools, "platform"), "file")));
        var metadata = recipeMetadata(root, profile.id(), request);
        var builder =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, metadata),
                        storage,
                        selected,
                        registry.sourceDirectory(profile),
                        stage.resolve("artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected Android acquisition: " + logical);
                        },
                        System.out::println);
        var receipts = new TreeMap<String, Object>();
        int preserved = 0;

        for (String role : List.of("LY_Launcher", "LY_Car", "LY_Video")) {
            Path original = storage.originals(profile.id()).resolve(role + ".apk");
            var raw = entries(original);
            String dex = role.equals("LY_Video") ? "classes2.dex" : "classes3.dex";
            checkPreflight(profile.id(), role, original, raw, dex);
            long builderStarted = System.nanoTime();
            Path output = builder.resolve("patched:" + role);
            long builderElapsed = (System.nanoTime() - builderStarted) / 1000000;
            System.out.println("ACTUAL_BUILDER_MS " + role + " " + builderElapsed);
            var after = entries(output);
            byte[] expectedDex =
                    expectedDex(profile.id(), role, original, raw, dex, after, expected);
            check(Arrays.equals(after.get(dex), expectedDex));
            preserved += preservedEntries(role, dex, raw, after);
            receipts.put(
                    role,
                    Map.of(
                            "file",
                            output.toString(),
                            "sha256",
                            FilesSupport.hash(output),
                            "bytes",
                            Files.size(output),
                            "global_two_pass_Artifacts_completed",
                            true,
                            "exact_expected_ear_DEX",
                            true));
        }

        registry.verifySource(profile);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "roles",
                                        receipts,
                                        "untargeted_entries_byte_preserved",
                                        preserved,
                                        "pass0_original_entries_unchanged",
                                        true,
                                        "actual_Artifacts_two_pass",
                                        true,
                                        "guest_started",
                                        false))
                        + "\n");
        System.out.println(
                "Actual Artifacts global raw preflight+apply3/3 PASS; preserved entries="
                        + preserved);
    }

    static Map<String, Object> recipeMetadata(Path root, String id, Map<String, Object> request)
            throws Exception {
        var metadata =
                Json.object(Json.parse(Files.readString(Path.of(Json.str(request, "patch_data")))));
        Path launcherRecipe =
                root.resolve(
                        "profiles/" + id + "/patches/" + PassionLauncherAudioGuard.NAME + ".json");
        var launcherMetadata = Json.object(Json.parse(Files.readString(launcherRecipe)));

        launcherMetadata.put("sourceSha256", FilesSupport.hash(launcherRecipe));
        metadata.put(PassionLauncherAudioGuard.NAME + ".json", launcherMetadata);
        Path ambientRecipe =
                root.resolve(
                        "profiles/" + id + "/patches/" + PassionAmbientLightGuard.NAME + ".json");
        var ambientMetadata = Json.object(Json.parse(Files.readString(ambientRecipe)));
        ambientMetadata.put("sourceSha256", FilesSupport.hash(ambientRecipe));
        metadata.put(PassionAmbientLightGuard.NAME + ".json", ambientMetadata);

        return metadata;
    }

    static void checkPreflight(
            String id, String role, Path original, Map<String, byte[]> raw, String dex)
            throws Exception {
        var snapshot = new TreeMap<String, String>();

        for (var e : raw.entrySet()) {
            snapshot.put(e.getKey(), NativeCore.hash(e.getValue()));
        }

        // The exact global preflight callback must preserve every original entry, including
        // non-code data.
        PassionBluetoothVolumeGuard.validateOriginalChain(id, 30, original, raw.get(dex));

        for (var e : raw.entrySet()) {
            check(snapshot.get(e.getKey()).equals(NativeCore.hash(e.getValue())));
        }

        if (role.equals("LY_Launcher")) {
            PassionLauncherAudioGuard.validateOriginalChain(id, 30, original, raw.get(dex));

            for (var e : raw.entrySet()) {
                check(snapshot.get(e.getKey()).equals(NativeCore.hash(e.getValue())));
            }
        }

        if (role.equals("LY_Car")) {
            for (String ambientDex : List.of("classes2.dex", "classes3.dex")) {
                PassionAmbientLightGuard.validateOriginalChain(
                        id, 30, original, ambientDex, raw.get(ambientDex));

                for (var e : raw.entrySet()) {
                    check(snapshot.get(e.getKey()).equals(NativeCore.hash(e.getValue())));
                }
            }
        }
    }

    static byte[] expectedDex(
            String id,
            String role,
            Path original,
            Map<String, byte[]> raw,
            String dex,
            Map<String, byte[]> after,
            Path expected)
            throws Exception {
        byte[] expectedDex = Files.readAllBytes(expected.resolve(role + ".dex"));

        if (role.equals("LY_Launcher")) {
            byte[] sidebar = PassionSidebarGuard.apply(id, 30, original, raw.get(dex));
            expectedDex = PassionBluetoothVolumeGuard.apply(id, 30, original, sidebar);
            expectedDex = PassionLauncherAudioGuard.apply(id, 30, original, expectedDex);
            int helper = 0;

            for (var entry : after.entrySet()) {
                if (entry.getKey().matches("classes[0-9]*\\.dex")) {
                    for (var cls : PassionBluetoothVolumeGuard.classes(entry.getValue()).keySet()) {
                        if (cls.equals(PassionSidebarGuard.ROUTER)) {
                            helper++;
                        }
                    }
                }
            }

            check(helper == 1);
        }

        if (role.equals("LY_Car")) {
            expectedDex = PassionAmbientLightGuard.apply(id, 30, original, expectedDex);
            byte[] prepared2 =
                    PassionVehicleBluetoothGuard.apply(id, 30, original, raw.get("classes2.dex"));
            check(
                    Arrays.equals(
                            after.get("classes2.dex"),
                            PassionAmbientLightGuard.apply(id, 30, original, prepared2)));
        }

        return expectedDex;
    }

    static int preservedEntries(
            String role, String dex, Map<String, byte[]> raw, Map<String, byte[]> after) {
        int preserved = 0;
        Set<String> mutable =
                role.equals("LY_Car")
                        ? Set.of(
                                "AndroidManifest.xml",
                                "classes.dex",
                                "classes2.dex",
                                "classes3.dex")
                        : Set.of("AndroidManifest.xml", dex);

        for (var e : raw.entrySet()) {
            if (!mutable.contains(e.getKey()) && !DexOperations.signatureEntry(e.getKey())) {
                check(Arrays.equals(e.getValue(), after.get(e.getKey())));
                preserved++;
            }
        }

        return preserved;
    }
}
