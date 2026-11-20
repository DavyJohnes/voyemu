package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/** Shared clean policy, two explicit profile bindings; no OEM icon/layout rewrite. */
public final class SecondaryNavigationPolicyTest {
    private interface Action {
        void run() throws Exception;
    }

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void denied(Action action) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("foreign binding accepted");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]),
                input = Path.of(args[1]),
                baseline = Path.of(args[2]),
                out = Path.of(args[3]);

        Files.createDirectory(out);
        Path freeFile =
                root.resolve("profiles/" + SecondaryNavigationGuard.PROFILE + "/profile.json");
        Path passionFile =
                root.resolve(
                        "profiles/"
                                + TestProfiles.declaring(root, PassionSecondaryNavigationGuard.NAME)
                                + "/profile.json");
        String freePin = FilesSupport.hash(freeFile), passionPin = FilesSupport.hash(passionFile);
        Map<String, Object> free = Json.object(Json.parse(Files.readString(freeFile))),
                passion = Json.object(Json.parse(Files.readString(passionFile)));
        check(
                Json.obj(free, "android").equals(Json.obj(passion, "android")),
                "profiles must use same registered clean base");
        byte[] dex;

        try (var zip = new ZipFile(input.toFile())) {
            dex = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }

        byte[] a = SecondaryNavigationGuard.apply(SecondaryNavigationGuard.PROFILE, 30, input, dex);
        byte[] b = PassionSecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 30, input, dex);
        check(
                !Arrays.equals(a, b),
                "Passion statusbar capability must be an explicit profile-only delta");
        check(
                Arrays.equals(a, Api30SecondaryNavigationPolicy.apply(30, input, dex, false)),
                "Free default mode changed");

        try (var zip = new ZipFile(baseline.toFile())) {
            check(
                    Arrays.equals(
                            a, zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes()),
                    "existing Free output changed");
        }

        rejectForeignBindings(input, dex, b);
        String name = PassionSecondaryNavigationGuard.NAME, step = name + ".json";
        Path recipe = passionFile.getParent();
        Map<String, Object> metadata =
                Json.object(Json.parse(Files.readString(recipe.resolve("patches/" + step))));
        metadata.put("sourceSha256", FilesSupport.hash(recipe.resolve("patches/" + step)));
        var nodes = new LinkedHashMap<>(Json.obj(passion, "patches"));
        nodes.put(
                name,
                Map.of(
                        "type",
                        "jar",
                        "input",
                        "android:/system/framework/services.jar",
                        "steps",
                        List.of(step)));
        var data = new LinkedHashMap<>(passion);
        data.put("patches", nodes);
        var profile =
                new ProfileRegistry.Profile(
                        TestProfiles.SYNTHETIC, "test-inactive", data, "", recipe);
        var adapter =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of(step, metadata)),
                        null,
                        null,
                        recipe,
                        out.resolve("work"),
                        path -> {
                            check(
                                    path.equals("/system/framework/services.jar"),
                                    "wrong platform input");

                            return input;
                        },
                        line -> {});
        Path jar = adapter.resolve("patched:" + name);
        checkResolvedEntries(jar, input, b);
        rejectMalformedRecipes(profile, recipe, out, input, name, metadata);
        check(
                FilesSupport.hash(freeFile).equals(freePin)
                        && FilesSupport.hash(passionFile).equals(passionPin),
                "active profile mutation");
        check(
                Json.obj(passion, "patches")
                        .get("LY_Launcher")
                        .equals(Json.obj(data, "patches").get("LY_Launcher")),
                "OEM right sidebar recipe altered");
        System.out.println(
                "Free exact baseline retained; explicit Passion status capability; real resolver"
                    + " and eleven negatives; profiles/right-sidebar untouched; no runtime claim");
    }

    private static void rejectForeignBindings(Path input, byte[] dex, byte[] b) throws Exception {
        denied(() -> SecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 30, input, dex));
        denied(() ->
                PassionSecondaryNavigationGuard.apply(
                        SecondaryNavigationGuard.PROFILE, 30, input, dex));
        denied(() -> PassionSecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 28, input, dex));
        denied(() -> PassionSecondaryNavigationGuard.apply("unknown", 30, input, dex));
        denied(() -> PassionSecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 30, input, b));
        denied(() ->
                PassionSecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 30, input, null));
    }

    private static void checkResolvedEntries(Path jar, Path input, byte[] b) throws Exception {
        var entries = NativeCore.entries(jar);
        var original = NativeCore.entries(input);

        check(
                Arrays.equals(entries.get("classes2.dex"), b),
                "Real resolver must execute explicit Passion mode");
        check(entries.keySet().equals(original.keySet()), "entry set changed");

        for (String key : original.keySet()) {
            if (!key.equals("classes2.dex")) {
                check(
                        Arrays.equals(entries.get(key), original.get(key)),
                        "resource/other DEX changed");
            }
        }
    }

    private static Map<String, Object> badMetadata(Map<String, Object> metadata, int mode) {
        var bad = new LinkedHashMap<>(metadata);

        if (mode == 0) {
            bad.put(
                    "calls",
                    List.of(
                            Map.of(
                                    "operation",
                                    "guardSecondaryNavigationDecorated",
                                    "args",
                                    List.of()),
                            Map.of(
                                    "operation",
                                    "guardSecondaryNavigationDecorated",
                                    "args",
                                    List.of())));
        }

        if (mode == 1) {
            bad.put(
                    "calls",
                    List.of(
                            Map.of(
                                    "operation",
                                    "guardSecondaryNavigationDecorated",
                                    "args",
                                    List.of("extra"))));
        }

        if (mode == 2) {
            bad.put("constants", Map.of("original_sha256_allowlist", List.of("0".repeat(64))));
        }

        if (mode == 3) {
            bad.put(
                    "calls",
                    List.of(
                            Map.of(
                                    "operation",
                                    "guardSecondaryNavigationSuppressed",
                                    "args",
                                    List.of())));
        }

        return bad;
    }

    private static void rejectMalformedRecipes(
            ProfileRegistry.Profile profile,
            Path recipe,
            Path out,
            Path input,
            String name,
            Map<String, Object> metadata)
            throws Exception {
        String step = name + ".json";

        for (int mode = 0; mode < 4; mode++) {
            var bad = badMetadata(metadata, mode);
            var invalid =
                    new Artifacts(
                            new Artifacts.PatchedProfile(profile, Map.of(step, bad)),
                            null,
                            null,
                            recipe,
                            out.resolve("bad" + mode),
                            path -> input,
                            line -> {});
            denied(() -> invalid.resolve("patched:" + name));
        }
    }
}
