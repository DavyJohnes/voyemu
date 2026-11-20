package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** The actual JAR resolver must apply navigation then the finite OEM top-gesture adapter. */
public final class SecondaryNavigationRecipeTest {
    static void check(boolean value, String why) {
        if (!value) {
            throw new AssertionError(why);
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), jar = Path.of(args[1]), stage = Path.of(args[2]);

        Files.createDirectory(stage);
        String step = SecondaryNavigationGuard.NAME + ".json";
        String id = TestProfiles.declaring(root, SecondaryNavigationGuard.NAME);
        Path recipe = root.resolve("profiles/" + id);
        var metadata = Json.object(Json.parse(Files.readString(recipe.resolve("patches/" + step))));
        metadata.put("sourceSha256", FilesSupport.hash(recipe.resolve("patches/" + step)));
        var data = Json.object(Json.parse(Files.readString(recipe.resolve("profile.json"))));
        var node =
                Map.of(
                        "type",
                        "jar",
                        "input",
                        "android:/system/framework/services.jar",
                        "steps",
                        List.of(step));
        check(
                Json.obj(data, "patches").get(SecondaryNavigationGuard.NAME).equals(node),
                "Existing exact clean services node unchanged");
        Path platform =
                args.length > 3
                        ? Path.of(args[3])
                        : root.resolve(
                                ".local/toolchains/java-sdk/platforms/android-34/android.jar");
        var tools =
                new Toolchains(
                        null,
                        Map.of(),
                        root.resolve(".local/toolchains/native-sdk"),
                        null,
                        null,
                        platform);
        var profile = new ProfileRegistry.Profile(id, "test", data, "", recipe);
        int[] reads = {0};
        var artifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of(step, metadata)),
                        null,
                        tools,
                        recipe,
                        stage.resolve("work"),
                        path -> {
                            check(
                                    path.equals("/system/framework/services.jar"),
                                    "Only exact Android dependency");
                            reads[0]++;

                            return jar;
                        },
                        line -> {});
        Path output = artifacts.resolve("patched:" + SecondaryNavigationGuard.NAME);
        var before = NativeCore.entries(jar);
        var after = NativeCore.entries(output);
        checkOutputEntries(before, after);
        check(
                reads[0] == 1
                        && artifacts
                                .resolve("patched:" + SecondaryNavigationGuard.NAME)
                                .equals(output)
                        && reads[0] == 1,
                "Resolver caches exact result");
        checkMalformedRecipes(profile, tools, recipe, stage, jar, step, metadata);
        int sourceRejects = checkSourceDrift(recipe, stage, profile, jar);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "actual_Artifacts_resolver",
                                        true,
                                        "one_existing_clean_JAR_node",
                                        true,
                                        "all_other_JAR_entries_exact",
                                        true,
                                        "startup_volume_classes_sha256",
                                        NativeCore.hash(after.get("classes.dex")),
                                        "ordered_output_classes2_sha256",
                                        NativeCore.hash(after.get("classes2.dex")),
                                        "malformed_recipes_before_helper_compile",
                                        8,
                                        "source_pin_negatives",
                                        sourceRejects,
                                        "guest_started",
                                        false))
                        + "\n");
        System.out.println(
                "PASS actual ordered JAR resolver, exact output, 8 malformed recipes before helper"
                        + " compile, 6 source-pin negatives; no guest");
    }

    static void checkOutputEntries(Map<String, byte[]> before, Map<String, byte[]> after)
            throws Exception {
        check(before.keySet().equals(after.keySet()), "JAR entry inventory retained");
        int changed = 0;

        for (String entry : before.keySet()) {
            if (!Arrays.equals(before.get(entry), after.get(entry))) {
                check(
                        entry.equals("classes.dex") || entry.equals("classes2.dex"),
                        "Only audio initialization and navigation DEX entries changed");
                changed++;
            }
        }

        check(
                changed == 2
                        && NativeCore.hash(after.get("classes.dex"))
                                .equals(
                                        "108491886c2ed3e8bdbd72849d6d5469796b9b1e5421f3f95e85a999505e2f00")
                        && NativeCore.hash(after.get("classes2.dex"))
                                .equals(
                                        "9605efe00d5ce96854c6a7078b75b03de0908f8a1053b408139ddaedc5a38cff"),
                "Actual ordered recipe output exact");
    }

    static Map<String, Object> badRecipe(
            Map<String, Object> metadata, int mode, Object nav, Object top) {
        var bad = new LinkedHashMap<String, Object>(metadata);

        switch (mode) {
            case 0 -> bad.put("calls", List.of(nav, nav));
            case 1 ->
                    bad.put(
                            "calls",
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardSecondaryNavigationSuppressed",
                                            "args",
                                            List.of("extra")),
                                    top));
            case 2 ->
                    bad.put(
                            "constants",
                            Map.of("original_sha256_allowlist", List.of("0".repeat(64))));
            case 3 -> bad.put("calls", List.of(top));
            case 4 -> bad.put("calls", List.of(top, nav));
            case 5 -> bad.put("calls", List.of(nav));
            case 6 -> bad.put("calls", List.of(nav, top, top));
            default ->
                    bad.put(
                            "calls",
                            List.of(nav, Map.of("operation", "foreign", "args", List.of())));
        }

        return bad;
    }

    static void checkMalformedRecipes(
            ProfileRegistry.Profile profile,
            Toolchains tools,
            Path recipe,
            Path stage,
            Path jar,
            String step,
            Map<String, Object> metadata)
            throws Exception {
        Object nav = Map.of("operation", "guardSecondaryNavigationSuppressed", "args", List.of()),
                top = Map.of("operation", "guardHiboardTopGesture", "args", List.of());

        for (int mode = 0; mode < 8; mode++) {
            var bad = badRecipe(metadata, mode, nav, top);
            Path work = stage.resolve("bad" + mode);
            var invalid =
                    new Artifacts(
                            new Artifacts.PatchedProfile(profile, Map.of(step, bad)),
                            null,
                            tools,
                            recipe,
                            work,
                            path -> jar,
                            line -> {});
            boolean refused = false;

            try {
                invalid.resolve("patched:" + SecondaryNavigationGuard.NAME);
            } catch (Failure expected) {
                refused = true;
            }

            check(
                    refused
                            && !Files.exists(
                                    work.resolve(
                                            "patched-"
                                                    + SecondaryNavigationGuard.NAME
                                                    + "/patched.jar"))
                            && !Files.exists(
                                    work.resolve(
                                            "patched-"
                                                    + SecondaryNavigationGuard.NAME
                                                    + "/hiboard-top-gesture")),
                    "Malformed recipe rejected before helper compilation/JAR output " + mode);
        }
    }

    static int checkSourceDrift(Path recipe, Path stage, ProfileRegistry.Profile profile, Path jar)
            throws Exception {
        Path source = recipe.resolve("patches/api30-hiboard-top-gesture");
        int sourceRejects = 0;

        for (int mode = 0; mode < 6; mode++) {
            Path copy = stage.resolve("source-bad" + mode);
            FilesSupport.copyTree(source, copy);

            if (mode < 4) {
                String name = HiboardTopGestureGuard.SOURCES.stream().sorted().toList().get(mode);
                Files.writeString(
                        copy.resolve(name),
                        Files.readString(copy.resolve(name)) + "\n// source drift\n");
            } else if (mode == 4) {
                Files.writeString(copy.resolve("foreign.java"), "class Foreign {}\n");
            } else {
                Files.delete(copy.resolve("src/com/android/server/wm/HiboardTopGesture.java"));
            }

            boolean refused = false;

            try {
                HiboardTopGestureGuard.validateOriginal(profile.id(), 30, jar, copy);
            } catch (Failure expected) {
                refused = true;
            }

            check(refused, "Compile-only/own source drift rejected " + mode);
            sourceRejects++;
        }

        return sourceRejects;
    }
}
