package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** The OEM Video of each profile takes the ten ARM64 libraries of the Gallery archive. */
public final class VideoArm64LibrariesTest {
    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), source = Path.of(args[1]), out = Path.of(args[2]);
        Path proposal = Path.of(args[3]);
        int checked = 0;

        Files.createDirectories(out);

        try (var entries = Files.list(proposal.resolve("profiles"))) {
            for (Path recipe : entries.sorted().toList()) {
                Path definition = recipe.resolve("profile.json");

                if (!Files.isRegularFile(definition)) {
                    continue;
                }

                var data = Json.object(Json.parse(Files.readString(definition)));

                if (!(Json.object(data.getOrDefault("patches", Map.of())).get("Video")
                        instanceof Map<?, ?> patch)) {
                    continue;
                }

                need(
                        patch.get("uses").equals(List.of("component:gallery-arm64-libraries"))
                                && patch.get("steps").equals(List.of("api30-gallery-arm64.json")));
                String id = recipe.getFileName().toString();
                var spec = Json.object(Json.obj(data, "original").get("Video"));
                Path original =
                        root.resolve(".local/profiles/" + id + "/originals/")
                                .resolve(Json.str(spec, "file"));

                if (!Files.isRegularFile(original)) {
                    System.out.println("SKIP " + id + ": OEM Video original absent");
                    continue;
                }

                need(FilesSupport.hash(original).equals(Json.str(spec, "sha256")));
                check(root, source, out.resolve(id), recipe, data, original);
                checked++;
            }
        }

        need(checked > 0);
        System.out.println("GREEN OEM Video ARM64 libraries of " + checked + " profiles PASS");
    }

    static void check(
            Path root,
            Path source,
            Path out,
            Path recipe,
            Map<String, Object> data,
            Path original)
            throws Exception {
        Files.createDirectories(out);
        var constants =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                recipe.resolve(
                                                        "patches/api30-gallery-arm64.json")))),
                        "constants");
        var baseline = NativeCore.entries(original);
        baseline.keySet().removeIf(DexOperations::signatureEntry);
        need(baseline.keySet().stream().noneMatch(n -> n.startsWith("lib/arm64-v8a/")));
        need(baseline.keySet().stream().filter(n -> n.startsWith("lib/armeabi-v7a/")).count() == 10);
        Path component = GalleryArm64Libraries.build(30, "arm64-v8a", source, out);
        var patched =
                GalleryArm64Libraries.add(
                        constants, 30, "arm64-v8a", original, baseline, component, source);
        need(patched.size() == baseline.size() + 10);

        for (var e : baseline.entrySet()) {
            need(java.util.Arrays.equals(e.getValue(), patched.get(e.getKey())));
        }

        for (String name : GalleryArm64Libraries.selected(source).keySet()) {
            need(baseline.containsKey(name.replace("lib/arm64-v8a/", "lib/armeabi-v7a/")));
        }

        need(
                GalleryArm64Libraries.same(
                        baseline,
                        GalleryArm64Libraries.remove(
                                constants, 30, "arm64-v8a", original, patched, component, source)));
        Path produced = build(root, source, out, recipe, data);
        var producedEntries = NativeCore.entries(produced);
        producedEntries.keySet().removeIf(DexOperations::signatureEntry);
        need(GalleryArm64Libraries.same(patched, producedEntries));
        System.out.println(
                "PASS "
                        + recipe.getFileName()
                        + ": Video keeps every OEM entry and adds the ten ARM64 libraries; SHA="
                        + FilesSupport.hash(produced));
    }

    // Builds patched:Video through Artifacts from the Video declarations of the profile.
    static Path build(Path root, Path source, Path out, Path recipe, Map<String, Object> data)
            throws Exception {
        Path privateCache = out.resolve("cache");
        Files.createDirectories(privateCache.resolve("sources"));
        var descriptor =
                Json.object(Json.parse(Files.readString(source.resolve("component.json"))));

        for (var entry : Json.obj(descriptor, "externalSources").entrySet()) {
            var pin = Json.object(entry.getValue());
            Files.createLink(
                    privateCache.resolve("sources/" + Json.str(pin, "sha256")),
                    source.resolve(entry.getKey()));
        }

        var selected =
                new ProfileRegistry.Profile(
                        recipe.getFileName().toString(),
                        "private",
                        Map.of(
                                "android",
                                data.get("android"),
                                "original",
                                Map.of("Video", Json.obj(data, "original").get("Video")),
                                "components",
                                Map.of(
                                        "gallery-arm64-libraries",
                                        "components/gallery-arm64-libraries"),
                                "patches",
                                Map.of("Video", Json.obj(data, "patches").get("Video")),
                                "install",
                                List.of(Map.of("artifact", "patched:Video", "type", "apk"))),
                        "",
                        recipe.getParent().getParent());
        Path step = recipe.resolve("patches/api30-gallery-arm64.json");
        var metadata = new java.util.LinkedHashMap<>(Json.object(Json.parse(Files.readString(step))));
        metadata.put("sourceSha256", FilesSupport.hash(step));
        var config = Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var signing = Json.obj(Json.obj(config, "signing"), "platform");
        var storage =
                new Storage(
                        root.resolve(".local"),
                        out.resolve("config"),
                        privateCache,
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
                        new Artifacts.PatchedProfile(
                                selected, Map.of("api30-gallery-arm64.json", metadata)),
                        storage,
                        tools,
                        recipe,
                        out.resolve("artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected Android input: " + logical);
                        },
                        System.out::println);

        return engine.build().get("patched:Video");
    }
}
