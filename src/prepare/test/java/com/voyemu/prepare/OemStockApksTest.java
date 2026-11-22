package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** Each OEM APK whose patch has no steps is installed with its OEM entries, re-signed only. */
public final class OemStockApksTest {
    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    /** Arguments: project root, output directory. */
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), out = Path.of(args[1]);
        int checked = 0;

        if (!hasSigningInputs(root)) {
            System.exit(77);
        }

        Files.createDirectories(out);

        try (var entries = Files.list(root.resolve("profiles"))) {
            for (Path recipe : entries.sorted().toList()) {
                Path definition = recipe.resolve("profile.json");

                if (!Files.isRegularFile(definition)) {
                    continue;
                }

                var data = Json.object(Json.parse(Files.readString(definition)));

                for (var node : Json.object(data.getOrDefault("patches", Map.of())).entrySet()) {
                    var patch = Json.object(node.getValue());

                    if (!patch.get("type").equals("apk")
                            || !Json.list(patch.get("steps")).isEmpty()) {
                        continue;
                    }

                    String id = recipe.getFileName().toString(), name = node.getKey();
                    need(
                            patch.get("input").equals("original:" + name)
                                    && !patch.containsKey("uses"));
                    need(
                            Json.list(data.get("install")).stream()
                                            .filter(i ->
                                                    Json.object(i)
                                                            .get("artifact")
                                                            .equals("patched:" + name))
                                            .count()
                                    == 1);
                    var spec = Json.object(Json.obj(data, "original").get(name));
                    Path original =
                            root.resolve(".local/profiles/" + id + "/originals/")
                                    .resolve(Json.str(spec, "file"));

                    if (!Files.isRegularFile(original)) {
                        System.out.println("SKIP " + id + " " + name + ": OEM original absent");
                        continue;
                    }

                    need(FilesSupport.hash(original).equals(Json.str(spec, "sha256")));
                    var baseline = NativeCore.entries(original);
                    baseline.keySet().removeIf(DexOperations::signatureEntry);
                    Path produced = build(root, out.resolve(id + "-" + name), recipe, data, name);
                    var actual = NativeCore.entries(produced);
                    actual.keySet().removeIf(DexOperations::signatureEntry);
                    need(GalleryArm64Libraries.same(baseline, actual));
                    System.out.println(
                            "PASS "
                                    + id
                                    + " "
                                    + name
                                    + ": "
                                    + baseline.size()
                                    + " OEM entries unchanged; SHA="
                                    + FilesSupport.hash(produced));
                    Files.delete(produced);
                    checked++;
                }
            }
        }

        if (checked == 0) {
            System.exit(77);
        }

        System.out.println("GREEN " + checked + " stock OEM APKs PASS");
    }

    static boolean hasSigningInputs(Path root) throws Exception {
        var config =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var signing = Json.obj(Json.obj(config, "signing"), "platform");
        Path cache = root.resolve(".local/toolchains/downloads/native");

        for (String artifact : List.of("key", "cert")) {
            var pin = Json.obj(signing, artifact);

            if (Files.notExists(cache.resolve(Json.str(pin, "sha256") + ".blob"))) {
                System.out.println("SKIP platform signing " + artifact + " absent");

                return false;
            }
        }

        return true;
    }

    // Builds patched:NAME through Artifacts from the declarations of the profile.
    static Path build(Path root, Path out, Path recipe, Map<String, Object> data, String name)
            throws Exception {
        var selected =
                new ProfileRegistry.Profile(
                        recipe.getFileName().toString(),
                        "private",
                        Map.of(
                                "android",
                                data.get("android"),
                                "original",
                                Map.of(name, Json.obj(data, "original").get(name)),
                                "components",
                                Map.of(),
                                "patches",
                                Map.of(name, Json.obj(data, "patches").get(name)),
                                "install",
                                List.of(Map.of("artifact", "patched:" + name, "type", "apk"))),
                        "",
                        recipe.getParent().getParent());
        var config =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var signing = Json.obj(Json.obj(config, "signing"), "platform");
        var storage =
                new Storage(
                        root.resolve(".local"),
                        out.resolve("config"),
                        out.resolve("cache"),
                        out.resolve("state"),
                        out.resolve("run"),
                        out.resolve("logs"));
        var tools =
                new Toolchains(
                        storage,
                        config,
                        root.resolve(".local/toolchains/native-sdk"),
                        root.resolve(
                                ".local/toolchains/downloads/native/"
                                        + Json.str(Json.obj(signing, "key"), "sha256")
                                        + ".blob"),
                        root.resolve(
                                ".local/toolchains/downloads/native/"
                                        + Json.str(Json.obj(signing, "cert"), "sha256")
                                        + ".blob"),
                        root.resolve(
                                ".local/toolchains/java-sdk/platforms/android-34/android.jar"));
        var engine =
                new Artifacts(
                        new Artifacts.PatchedProfile(selected, Map.of()),
                        storage,
                        tools,
                        recipe,
                        out.resolve("artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected Android input: " + logical);
                        },
                        System.out::println);

        return engine.build().get("patched:" + name);
    }
}
