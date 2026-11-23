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

    /** Arguments: project root, output directory. */
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), out = Path.of(args[1]);
        Path archives = root.resolve(".local/toolchains/downloads/sources");
        int checked = 0;

        if (!OemStockApksTest.hasSigningInputs(root)) {
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
                    Path descriptor = recipe.resolve("components/" + component + "/component.json");
                    Path source = out.resolve(id + "-" + name).resolve("source");

                    if (stageInputs(descriptor, archives, source)) {
                        check(root, source, recipe, data, name, original);
                        checked++;
                    } else {
                        System.out.println("SKIP " + id + " " + name + ": pinned AAR absent");
                    }
                }
            }
        }

        if (checked == 0) {
            System.exit(77);
        }

        System.out.println("GREEN " + checked + " OEM APKs with ARM64 library archives PASS");
    }

    static boolean stageInputs(Path descriptor, Path archives, Path source) throws Exception {
        var external =
                Json.obj(Json.object(Json.parse(Files.readString(descriptor))), "externalSources");
        var cached = new java.util.LinkedHashMap<String, Path>();

        for (var entry : external.entrySet()) {
            var pin = Json.object(entry.getValue());
            Path archive = archives.resolve(Json.str(pin, "sha256"));

            if (Files.notExists(archive)) {
                return false;
            }

            cached.put(entry.getKey(), archive);
        }

        Files.createDirectories(source);
        Files.copy(descriptor, source.resolve("component.json"));

        for (var archive : cached.entrySet()) {
            Files.copy(archive.getValue(), source.resolve(archive.getKey()));
        }

        return true;
    }

    // SOURCE holds the component descriptor and its archives; its parent is the work directory.
    static void check(
            Path root,
            Path source,
            Path recipe,
            Map<String, Object> data,
            String name,
            Path original)
            throws Exception {
        Path out = source.getParent();
        var patch = Json.obj(Json.obj(data, "patches"), name);
        var steps = Json.list(patch.get("steps"));

        boolean dvrFramework =
                recipe.getFileName().toString().equals("free-11-chinese-20260731-7.3.1")
                        && name.equals("Dvr");
        boolean usbMedia =
                data.get("features") instanceof Map<?, ?> features
                        && Boolean.TRUE.equals(features.get("usb_test_volume_visibility"))
                        && (name.equals("Video") || name.equals("Gallery"));

        need(
                dvrFramework
                        ? steps.equals(
                                List.of(
                                        "api30-dvr-arm64.json",
                                        "api30-framework-libraries.json",
                                        "api30-dvr-camera-virtual.json"))
                        : usbMedia
                                ? steps.equals(
                                        List.of(
                                                "api30-gallery-arm64.json",
                                                name.equals("Video")
                                                        ? "usb-video-port-metadata.json"
                                                        : "usb-gallery-port-metadata.json"))
                                : steps.size() == 1);
        String step = (String) steps.get(0);
        var metadata = Json.object(Json.parse(Files.readString(recipe.resolve("patches/" + step))));
        var constants = Json.obj(metadata, "constants");
        int api = Math.toIntExact(Json.integer(metadata, "api"));
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
        Path archive = GalleryArm64Libraries.build(api, "arm64-v8a", source, out);
        var patched =
                GalleryArm64Libraries.add(
                        constants, api, "arm64-v8a", original, baseline, archive, source);
        need(patched.size() == baseline.size() + expected.size());

        for (var e : baseline.entrySet()) {
            need(java.util.Arrays.equals(e.getValue(), patched.get(e.getKey())));
        }

        need(
                GalleryArm64Libraries.same(
                        baseline,
                        GalleryArm64Libraries.remove(
                                constants, api, "arm64-v8a", original, patched, archive, source)));
        Path produced = build(root, source, recipe, data, name);

        if (dvrFramework) {
            var libraries =
                    Json.object(
                            Json.parse(
                                    Files.readString(
                                            recipe.resolve(
                                                    "patches/api30-framework-libraries.json"))));
            libraries.put(
                    "sourceSha256",
                    FilesSupport.hash(recipe.resolve("patches/api30-framework-libraries.json")));
            var target = new FrameworkLibrariesTest.Target();
            target.profile = recipe.getFileName().toString();
            target.pin = FilesSupport.hash(original);
            Recipes.apply("api30-framework-libraries.json", libraries, target);
            need(target.installed.equals(List.of("QGAPI")));
            patched.put(
                    "AndroidManifest.xml",
                    ManifestLibraries.add(patched.get("AndroidManifest.xml"), target.installed));
        }

        var producedEntries = NativeCore.entries(produced);
        producedEntries.keySet().removeIf(DexOperations::signatureEntry);

        if (dvrFramework) {
            Path cameraRecipe = recipe.resolve("patches/api30-dvr-camera-virtual.json");
            var cameraConstants =
                    Json.obj(Json.object(Json.parse(Files.readString(cameraRecipe))), "constants");
            patched.put(
                    "classes2.dex",
                    DvrCameraVirtualGuard.patch(
                            cameraConstants,
                            api,
                            "arm64-v8a",
                            original,
                            patched.get("classes2.dex"),
                            true));
            var helpers =
                    producedEntries.keySet().stream()
                            .filter(n ->
                                    n.matches("classes[0-9]+\\.dex") && !patched.containsKey(n))
                            .toList();
            need(helpers.size() == 1);
            need(
                    CameraVirtualGuard.classes(producedEntries.remove(helpers.getFirst()))
                            .keySet()
                            .equals(java.util.Set.of(DvrCameraVirtualGuard.HELPER)));
        } else if (usbMedia) {
            var layout =
                    name.equals("Video")
                            ? Free731UsbGuard.Layout.VIDEO
                            : Free731UsbGuard.Layout.GALLERY;
            String slot = layout == Free731UsbGuard.Layout.VIDEO ? "classes.dex" : "classes2.dex";
            patched.put(slot, Free731UsbGuard.apply(baseline.get(slot), layout));
            var extra = new java.util.TreeSet<>(producedEntries.keySet());
            extra.removeAll(patched.keySet());
            need(extra.size() == 1);
            String helperDex = extra.first();
            need(helperDex.matches("classes[2-9][0-9]*\\.dex"));
            var classes =
                    new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                    org.jf.dexlib2.Opcodes.forApi(30),
                                    producedEntries.remove(helperDex))
                            .getClasses();
            need(
                    classes.size() == 1
                            && classes.iterator()
                                    .next()
                                    .getType()
                                    .equals("Lcom/voyemu/qgapi/VoyemuUsbPort;"));
        }

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
    static Path build(Path root, Path source, Path recipe, Map<String, Object> data, String name)
            throws Exception {
        Path out = source.getParent();
        String component =
                ((String) Json.list(Json.obj(Json.obj(data, "patches"), name).get("uses")).get(0))
                        .substring("component:".length());
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
                                "features",
                                data.getOrDefault("features", Map.of()),
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
        var recipeMetadata = new java.util.LinkedHashMap<String, Object>();

        for (Object declared : Json.list(Json.obj(Json.obj(data, "patches"), name).get("steps"))) {
            String declaredStep = (String) declared;
            Path stepFile = recipe.resolve("patches/" + declaredStep);
            var metadata =
                    new java.util.LinkedHashMap<>(
                            Json.object(Json.parse(Files.readString(stepFile))));
            metadata.put("sourceSha256", FilesSupport.hash(stepFile));
            recipeMetadata.put(declaredStep, metadata);
        }

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
                        new Artifacts.PatchedProfile(selected, recipeMetadata),
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
