package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/**
 * Each OEM APK whose patch takes an ARM64 library archive keeps every OEM entry and gains exactly
 * the ARM64 twin of each of its ARM32 libraries.
 */
public final class OemArm64LibrariesTest {
    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    /** Arguments: project root, directory of the pinned AARs by archive name, output, proposal. */
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), archives = Path.of(args[1]), out = Path.of(args[2]);
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

                for (var node : Json.object(data.getOrDefault("patches", Map.of())).entrySet()) {
                    var patch = Json.object(node.getValue());

                    if (!(patch.get("uses") instanceof List<?> uses
                            && uses.size() == 1
                            && uses.get(0) instanceof String use
                            && use.startsWith("component:")
                            && use.endsWith("-arm64-libraries"))) {
                        continue;
                    }

                    String id = recipe.getFileName().toString(), name = node.getKey();
                    var spec = Json.object(Json.obj(data, "original").get(name));
                    Path original =
                            root.resolve(".local/profiles/" + id + "/originals/")
                                    .resolve(Json.str(spec, "file"));

                    if (!Files.isRegularFile(original)) {
                        System.out.println("SKIP " + id + " " + name + ": OEM original absent");
                        continue;
                    }

                    need(FilesSupport.hash(original).equals(Json.str(spec, "sha256")));
                    String component = use.substring("component:".length());
                    Path work = out.resolve(id + "-" + name);
                    Path source = work.resolve("source");
                    Files.createDirectories(source);
                    Path descriptor = recipe.resolve("components/" + component + "/component.json");
                    Files.copy(descriptor, source.resolve("component.json"));

                    for (String archive :
                            Json.obj(
                                            Json.object(Json.parse(Files.readString(descriptor))),
                                            "externalSources")
                                    .keySet()) {
                        Files.createLink(source.resolve(archive), archives.resolve(archive));
                    }

                    check(root, source, work, recipe, data, name, component, original);
                    checked++;
                }
            }
        }

        need(checked > 0);
        System.out.println("GREEN " + checked + " OEM APKs with ARM64 library archives PASS");
    }

    static void check(
            Path root,
            Path source,
            Path out,
            Path recipe,
            Map<String, Object> data,
            String name,
            String component,
            Path original)
            throws Exception {
        var patch = Json.obj(Json.obj(data, "patches"), name);
        var steps = Json.list(patch.get("steps"));
        need(steps.size() == 1);
        String step = (String) steps.get(0);
        var constants =
                Json.obj(
                        Json.object(
                                Json.parse(Files.readString(recipe.resolve("patches/" + step)))),
                        "constants");
        var baseline = NativeCore.entries(original);
        baseline.keySet().removeIf(DexOperations::signatureEntry);
        var expected = GalleryArm64Libraries.selected(source);
        need(baseline.keySet().stream().noneMatch(n -> n.startsWith("lib/arm64-v8a/")));
        need(
                baseline.keySet().stream()
                        .filter(n -> n.startsWith("lib/armeabi-v7a/"))
                        .toList()
                        .equals(
                                expected.keySet().stream()
                                        .map(n -> n.replace("lib/arm64-v8a/", "lib/armeabi-v7a/"))
                                        .toList()));
        Path archive = GalleryArm64Libraries.build(30, "arm64-v8a", source, out);
        var patched =
                GalleryArm64Libraries.add(
                        constants, 30, "arm64-v8a", original, baseline, archive, source);
        need(patched.size() == baseline.size() + expected.size());

        for (var e : baseline.entrySet()) {
            need(java.util.Arrays.equals(e.getValue(), patched.get(e.getKey())));
        }

        need(
                GalleryArm64Libraries.same(
                        baseline,
                        GalleryArm64Libraries.remove(
                                constants, 30, "arm64-v8a", original, patched, archive, source)));
        Path produced = build(root, source, out, recipe, data, name, component, step);
        var producedEntries = NativeCore.entries(produced);
        producedEntries.keySet().removeIf(DexOperations::signatureEntry);
        need(GalleryArm64Libraries.same(patched, producedEntries));
        System.out.println(
                "PASS "
                        + recipe.getFileName()
                        + " "
                        + name
                        + ": every OEM entry kept, "
                        + expected.size()
                        + " ARM64 libraries added; SHA="
                        + FilesSupport.hash(produced));
    }

    // Builds the patched APK through Artifacts from the declarations of the profile.
    static Path build(
            Path root,
            Path source,
            Path out,
            Path recipe,
            Map<String, Object> data,
            String name,
            String component,
            String step)
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
                                Map.of(name, Json.obj(data, "original").get(name)),
                                "components",
                                Map.of(component, "components/" + component),
                                "patches",
                                Map.of(name, Json.obj(data, "patches").get(name)),
                                "install",
                                List.of(Map.of("artifact", "patched:" + name, "type", "apk"))),
                        "",
                        recipe.getParent().getParent());
        Path stepFile = recipe.resolve("patches/" + step);
        var metadata =
                new java.util.LinkedHashMap<>(Json.object(Json.parse(Files.readString(stepFile))));
        metadata.put("sourceSha256", FilesSupport.hash(stepFile));
        var config =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
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
                        new Artifacts.PatchedProfile(selected, Map.of(step, metadata)),
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
