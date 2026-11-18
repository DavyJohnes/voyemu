package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;

/** Exercises the production file-artifact resolver without Android, SDK calls or a VM. */
public final class Qinggan09EquipmentArtifactsTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);
        String id = "free-09-chinese-20260106-6.0.3";
        Path recipe = root.resolve("profiles").resolve(id);
        String definition = Files.readString(recipe.resolve("profile.json"));
        var data = Json.object(Json.parse(definition));
        byte[] original = Files.readAllBytes(recipe.resolve("files/nvram.txt"));
        var artifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(
                                new ProfileRegistry.Profile(id, "test", data, ""), Map.of()),
                        null,
                        null,
                        recipe,
                        work,
                        null,
                        value -> {});
        Path raw = artifacts.resolve("files:hybrid-nvram");
        Path jni = artifacts.resolve("files:nvram");

        if (Files.size(raw) != 4194304
                || !jni.equals(recipe.resolve("files/nvram.txt"))
                || !Arrays.equals(original, Files.readAllBytes(jni))) {
            throw new AssertionError("Artifact extent/shared seed/source immutability");
        }

        byte[] image = Files.readAllBytes(raw);
        Qinggan09EquipmentNvram.validate(image, Files.readString(jni), "hybrid");

        if (!artifacts.resolve("files:hybrid-nvram").equals(raw)) {
            throw new AssertionError("Resolved artifact cache");
        }

        for (String property : new String[] {"powertrain", "kind"}) {
            var invalid = Json.object(Json.parse(definition));
            var files = Json.obj(invalid, "files");
            var rawSpec = Json.obj(files, "hybrid-nvram");
            var simulation = Json.obj(rawSpec, "simulation");
            simulation.put(property, "unknown");
            rawSpec.put("simulation", simulation);
            files.put("hybrid-nvram", rawSpec);
            invalid.put("files", files);
            reject(id, invalid, recipe, work.resolve(property));
        }

        var wrongSeed = Json.object(Json.parse(definition));
        var seedFiles = Json.obj(wrongSeed, "files");
        var seedRaw = Json.obj(seedFiles, "hybrid-nvram");
        seedRaw.put("file", "early-inputs.rc");
        seedFiles.put("hybrid-nvram", seedRaw);
        wrongSeed.put("files", seedFiles);
        reject(id, wrongSeed, recipe, work.resolve("seed"));
        var wrongApi = Json.object(Json.parse(definition));
        var android = Json.obj(wrongApi, "android");
        android.put("package", "system-images;android-30;default;arm64-v8a");
        wrongApi.put("android", android);
        reject(id, wrongApi, recipe, work.resolve("api"));
        System.out.println(
                "A9 equipment actual Artifacts.resolve PASS: raw/JNI sharing, immutable source,"
                        + " cached result, 4 invalid graph cases");
    }

    private static void reject(String id, Map<String, Object> data, Path recipe, Path work)
            throws Exception {
        try {
            new Artifacts(
                            new Artifacts.PatchedProfile(
                                    new ProfileRegistry.Profile(id, "test", data, ""), Map.of()),
                            null,
                            null,
                            recipe,
                            work,
                            null,
                            value -> {})
                    .resolve("files:hybrid-nvram");
        } catch (Failure expected) {
            if (!expected.code.equals("EQUIPMENT_SIMULATION_INVALID")) {
                throw expected;
            }

            return;
        }

        throw new AssertionError("Invalid equipment graph accepted");
    }
}
