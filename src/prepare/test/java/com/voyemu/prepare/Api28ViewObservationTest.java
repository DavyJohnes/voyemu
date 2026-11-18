package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Every API28 profile builds the same framework bridge and replaces the full boot classpath. */
public final class Api28ViewObservationTest {
    private static final String BASE = "system-images;android-28;default;arm64-v8a";

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static Map<String, Object> json(Path file) throws Exception {
        return Json.object(Json.parse(Files.readString(file)));
    }

    /** Materializes and compiles view-observation the way component preparation does. */
    private static byte[] bridge(
            Path root, String id, Map<String, Object> data, Path platform, Path work)
            throws Exception {
        var profile =
                new ProfileRegistry.Profile(id, "test", data, "voyemu/profiles/" + id + "/", root);
        Path original =
                root.resolve(
                        "profiles/"
                                + id
                                + "/"
                                + Json.str(Json.obj(data, "components"), "view-observation"));
        Path source = work.resolve(id).resolve("source");

        FilesSupport.copyTree(original, source);
        var definition = json(source.resolve("component.json"));
        check(
                Json.str(definition, "type").equals("jar")
                        && Json.integer(definition, "minSdk") == 28
                        && Json.str(definition, "output").equals("view-observation.jar"),
                id + ": API28 bridge jar component required");

        if (definition.containsKey("sourceFiles")) {
            ComponentSources.copy(
                    Json.obj(definition, "sourceFiles"),
                    source,
                    resource -> ProfileRegistry.bytes(profile, resource));
        }

        Path dex =
                DexOperations.javaDex(
                        source.resolve("src"),
                        null,
                        List.of(),
                        platform,
                        work.resolve(id).resolve("java"),
                        28);
        var entries = FilesSupport.tree(dex);
        check(entries.keySet().equals(java.util.Set.of("classes.dex")), id + ": one helper DEX");
        byte[] helper = Files.readAllBytes(entries.get("classes.dex"));
        check(
                NativeCore.classes(
                                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                        org.jf.dexlib2.Opcodes.forApi(28), helper))
                        .contains(ViewDumpPatch.HELPER),
                id + ": bridge class missing");

        return helper;
    }

    /** Boot classpath components keyed by name; each one is installed over its own jar. */
    private static Map<String, Object> bootClasspath(Path root, String id, Map<String, Object> data)
            throws Exception {
        Map<String, Object> result = new TreeMap<>();
        Map<String, String> installed = new TreeMap<>();

        for (Object item : Json.list(data.get("install"))) {
            var entry = Json.object(item);

            if (Json.str(entry, "artifact").startsWith("component:")
                    && "file".equals(entry.get("type"))) {
                installed.put(
                        Json.str(entry, "artifact").substring("component:".length()),
                        Json.str(entry, "target") + " " + entry.get("mode"));
            }
        }

        for (var component : Json.obj(data, "components").entrySet()) {
            var definition =
                    json(
                            root.resolve(
                                    "profiles/"
                                            + id
                                            + "/"
                                            + component.getValue()
                                            + "/component.json"));

            if (!definition.containsKey("frameworkBase")) {
                continue;
            }

            var base = Json.obj(definition, "frameworkBase");

            if (!Json.str(base, "oat").startsWith("/system/framework/arm64/")) {
                continue;
            }

            check(
                    (Json.str(base, "jar") + " 0644").equals(installed.get(component.getKey())),
                    id
                            + ": boot classpath component not installed over its jar: "
                            + component.getKey());
            result.put(component.getKey(), base);
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath(), platform = Path.of(args[1]);
        Path work = Path.of(args[2]);
        Map<String, Map<String, Object>> profiles = new TreeMap<>();

        try (var folders = Files.list(root.resolve("profiles"))) {
            for (Path folder : folders.sorted().toList()) {
                Path file = folder.resolve("profile.json");

                if (Files.isRegularFile(file)) {
                    var data = json(file);
                    var android = data.get("android");

                    if (android instanceof Map<?, ?>
                            && BASE.equals(Json.object(android).get("package"))) {
                        profiles.put(folder.getFileName().toString(), data);
                    }
                }
            }
        }

        check(
                profiles.keySet()
                        .containsAll(
                                java.util.Set.of(
                                        "free-09-chinese-20260106-6.0.3",
                                        "free-09-chinese-20221230-nxp-rc45")),
                "API28 profiles not discovered: " + profiles.keySet());
        byte[] reference = null;
        Map<String, Object> referenceBoot = null;
        String referenceImage = null;

        for (var profile : profiles.entrySet()) {
            String id = profile.getKey();
            var data = profile.getValue();
            var boot = bootClasspath(root, id, data);
            check(boot.size() == 15, id + ": full boot classpath replacement required");
            check(
                    "component:view-observation"
                            .equals(Json.object(boot.get("base-framework")).get("bridge")),
                    id + ": framework base must host the view bridge");
            byte[] helper = bridge(root, id, data, platform, work);
            String image = Json.str(Json.obj(data, "android"), "sha256");

            if (reference == null) {
                reference = helper;
                referenceBoot = boot;
                referenceImage = image;
            } else {
                check(Arrays.equals(reference, helper), id + ": bridge differs between profiles");
                check(
                        !image.equals(referenceImage) || boot.equals(referenceBoot),
                        id + ": boot classpath pins differ on the same Android image");
            }

            byte[] source = ViewDumpPatchTest.source(42, 4, "");
            byte[] jumbo = FrameworkBridgeVerifier.jumboHelper(helper);
            byte[] patched =
                    ViewDumpPatch.patch(
                            source, ViewDumpPatch.sha(source), jumbo, ViewDumpPatch.sha(jumbo));
            FrameworkBridgeVerifier.verify(source, jumbo, patched);
        }

        System.out.println(
                "API28 profiles "
                        + profiles.keySet()
                        + " build one view bridge accepted by the framework patch and replace the"
                        + " full boot classpath; no guest acceptance");
    }
}
