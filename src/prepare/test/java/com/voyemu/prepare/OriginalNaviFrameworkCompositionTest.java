package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Exercise the exact production bridge output as the downstream EGL recipe input. */
public final class OriginalNaviFrameworkCompositionTest {
    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        require(
                args.length == 5,
                "ROOT ORIGINAL_FRAMEWORK PRODUCTION_BRIDGE PRODUCTION_FRAMEWORK OUTPUT required");
        Path root = Path.of(args[0]),
                original = Path.of(args[1]),
                bridge = Path.of(args[2]),
                actual = Path.of(args[3]),
                output = Path.of(args[4]);
        var profile =
                new ProfileRegistry(root, Api30ViewObservation.PROFILE)
                        .get(Api30ViewObservation.PROFILE);
        var component =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + Api30ViewObservation.PROFILE
                                                        + "/components/view-observation/component.json"))));
        Api30ViewObservation.source(profile, component);
        Api30ViewObservation.build(Api30ViewObservation.PROFILE, 30, original, bridge, output);
        require(
                FilesSupport.hash(output).equals(FilesSupport.hash(actual)),
                "Rebuilt framework differs from actual production output");
        var before = NativeCore.entries(original);
        var after = NativeCore.entries(output);
        require(before.keySet().equals(after.keySet()), "Framework entries changed");

        for (var entry : before.entrySet()) {
            if (!entry.getKey().equals("classes3.dex")) {
                require(
                        Arrays.equals(entry.getValue(), after.get(entry.getKey())),
                        "Unrelated framework entry changed: " + entry.getKey());
            }
        }

        var egl =
                OriginalNaviEglGuard.apply(
                        Api30ViewObservation.PROFILE, 30, after.get("classes4.dex"));
        require(
                Arrays.equals(
                        egl,
                        OriginalNaviEglGuard.apply(
                                Api30ViewObservation.PROFILE, 30, before.get("classes4.dex"))),
                "Bridge composition changed the EGL transform output");
        Path recipe =
                root.resolve(
                        "profiles/"
                                + Api30ViewObservation.PROFILE
                                + "/patches/original-navi-egl.json");
        Map<String, Object> metadata =
                new LinkedHashMap<>(Json.object(Json.parse(Files.readString(recipe))));
        metadata.put("sourceSha256", FilesSupport.hash(recipe));
        var target = new OriginalNaviRecipesTest.Target(FilesSupport.hash(actual));
        Recipes.validateApi30(recipe.getFileName().toString(), metadata, target);
        Recipes.apply(recipe.getFileName().toString(), metadata, target);
        require(
                target.validate == 2 && target.apply == 1,
                "Exact current downstream recipe not applied");

        var originalInput = new OriginalNaviRecipesTest.Target(FilesSupport.hash(actual));
        originalInput.derived = false;
        boolean unpinned = false;

        try {
            Recipes.validateApi30(recipe.getFileName().toString(), metadata, originalInput);
        } catch (Failure expected) {
            unpinned = true;
        }

        require(unpinned, "Unpinned original framework accepted");
        var widened = new LinkedHashMap<>(metadata);
        widened.put(
                "constants",
                Map.of(
                        "original_sha256_allowlist",
                        List.of(
                                FilesSupport.hash(actual),
                                "edb771bc803f1c8505f0ba96ffad67b9f4af84db01e896bf5790ae75b14b3a6d")));
        boolean denied = false;

        try {
            Recipes.validateApi30(
                    recipe.getFileName().toString(),
                    widened,
                    new OriginalNaviRecipesTest.Target(FilesSupport.hash(actual)));
        } catch (Failure expected) {
            denied = true;
        }

        require(denied, "Pinned derived framework accepted");
        System.out.println(
                "PASS exact production framework derivation; unrelated entries/EGL unchanged;"
                        + " current downstream and unpinned original/pinned derived negatives");
    }
}
