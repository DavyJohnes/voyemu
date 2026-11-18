package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class GalleryArm64LibrariesTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    interface Check {
        void run() throws Exception;
    }

    static int negatives;

    static void reject(Check c) throws Exception {
        boolean failed = false;

        try {
            c.run();
        } catch (Failure | IllegalArgumentException expected) {
            failed = true;
        }

        if (!failed) {
            throw new AssertionError("Expected rejection");
        }

        negatives++;
    }

    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), source = Path.of(args[1]), out = Path.of(args[2]);

        Files.createDirectories(out);
        Path original =
                root.resolve(
                        ".local/profiles/free-11-chinese-20260731-7.3.1/originals/Gallery.apk");
        var baseline = NativeCore.entries(original);
        baseline.keySet().removeIf(DexOperations::signatureEntry);
        boolean red = baseline.keySet().stream().noneMatch(n -> n.startsWith("lib/arm64-v8a/"));
        need(red);
        System.out.println("RED original has no ARM64 codec entries: reproduced");
        var expected = GalleryArm64Libraries.selected(source);
        need(expected.size() == 10);
        Path component = GalleryArm64Libraries.build(PROFILE, 30, "arm64-v8a", source, out);
        var patched =
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", original, baseline, component, source);
        need(patched.size() == baseline.size() + 10);

        for (var e : baseline.entrySet()) {
            need(java.util.Arrays.equals(e.getValue(), patched.get(e.getKey())));
        }

        need(
                baseline.keySet().stream().filter(n -> n.startsWith("lib/armeabi-v7a/")).count()
                        == 10);
        need(
                GalleryArm64Libraries.same(
                        baseline,
                        GalleryArm64Libraries.remove(
                                PROFILE, 30, "arm64-v8a", original, patched, component, source)));
        checkRejectedContexts(original, baseline, patched, expected, component, source);
        Path badSource = out.resolve("bad-source");
        Files.createDirectories(badSource);
        String originalDefinition = Files.readString(source.resolve("component.json"));

        for (String archive : List.of("ijk.aar", "fmmr.aar")) {
            Files.createLink(badSource.resolve(archive), source.resolve(archive));
        }

        checkRejectedExternalSources(badSource, originalDefinition);
        checkRejectedLibraries(badSource, originalDefinition);
        checkRejectedInputs(original, baseline, expected, component, source, out);
        Path unsigned = out.resolve("unsigned.apk");
        NativeCore.zip(unsigned, patched, true);
        var config =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var signing = Json.obj(Json.obj(config, "signing"), "platform");
        byte[]
                key =
                        Files.readAllBytes(
                                root.resolve(
                                        ".local/toolchains/downloads/native/"
                                                + Json.str(Json.obj(signing, "key"), "sha256")
                                                + ".blob")),
                cert =
                        Files.readAllBytes(
                                root.resolve(
                                        ".local/toolchains/downloads/native/"
                                                + Json.str(Json.obj(signing, "cert"), "sha256")
                                                + ".blob"));
        Path signed =
                DexOperations.sign(unsigned, out.resolve("Gallery-signed.apk"), key, cert, 30);
        var actual = NativeCore.entries(signed);
        actual.keySet().removeIf(DexOperations::signatureEntry);
        need(GalleryArm64Libraries.same(patched, actual));
        Files.delete(unsigned);

        if (args.length == 4) {
            checkArtifactsRoute(args, root, source, out, config, signing, patched);
        }

        System.out.println(
                "GREEN forward/inverse/all original entry parity, 10 ARM32 preserved + 10 exact"
                        + " ARM64, signed APK verification PASS; negatives="
                        + negatives
                        + "; signedSHA="
                        + FilesSupport.hash(signed));
    }

    static void checkRejectedContexts(
            Path original,
            Map<String, byte[]> baseline,
            Map<String, byte[]> patched,
            Map<String, byte[]> expected,
            Path component,
            Path source)
            throws Exception {
        reject(() -> GalleryArm64Libraries.context("foreign", 30, "arm64-v8a"));
        reject(() -> GalleryArm64Libraries.context(PROFILE, 28, "arm64-v8a"));
        reject(() -> GalleryArm64Libraries.context(PROFILE, 30, "armeabi-v7a"));
        reject(() ->
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", original, patched, component, source));
        var corrupt = new TreeMap<>(patched);
        corrupt.put(expected.keySet().iterator().next(), new byte[20]);
        reject(() ->
                GalleryArm64Libraries.remove(
                        PROFILE, 30, "arm64-v8a", original, corrupt, component, source));
        var downgrade = new TreeMap<>(baseline);
        downgrade.remove(
                downgrade.keySet().stream()
                        .filter(n -> n.startsWith("lib/armeabi-v7a/"))
                        .findFirst()
                        .orElseThrow());
        reject(() ->
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", original, downgrade, component, source));
    }

    static void checkRejectedExternalSources(Path badSource, String originalDefinition)
            throws Exception {
        for (int fault = 0; fault < 8; fault++) {
            var d = Json.object(Json.parse(originalDefinition));
            var external = Json.obj(d, "externalSources");
            var spec = new java.util.LinkedHashMap<>(Json.object(external.get("ijk.aar")));
            var modified = new java.util.LinkedHashMap<>(external);

            if (fault == 0) {
                spec.put("kind", "source");
            }

            if (fault == 1) {
                spec.put("format", "zip");
            }

            if (fault == 2) {
                spec.put("bytes", 0);
            }

            if (fault == 3) {
                spec.put("bytes", 16L * 1024 * 1024 + 1);
            }

            if (fault == 4) {
                spec.put("sha256", "f".repeat(64));
            }

            if (fault == 5) {
                spec.put("url", "http://example.org/input.aar");
            }

            if (fault == 6) {
                spec.put("extra", true);
            }

            if (fault == 7) {
                spec.put("bytes", Json.integer(spec, "bytes") + 1);
            }

            modified.put("ijk.aar", spec);
            d.put("externalSources", modified);
            Files.writeString(badSource.resolve("component.json"), Json.write(d));
            reject(() -> GalleryArm64Libraries.selected(badSource));
        }
    }

    static void checkRejectedLibraries(Path badSource, String originalDefinition) throws Exception {
        for (int fault = 0; fault < 4; fault++) {
            var d = Json.object(Json.parse(originalDefinition));
            var table = new java.util.LinkedHashMap<>(Json.obj(d, "libraries"));
            String name = table.keySet().iterator().next();
            var spec = new java.util.LinkedHashMap<>(Json.object(table.get(name)));

            if (fault == 0) {
                spec.put("sha256", "f".repeat(64));
            }

            if (fault == 1) {
                spec.put("bytes", 1);
            }

            if (fault == 2) {
                spec.put("entry", "jni/arm64-v8a/../escape.so");
            }

            if (fault == 3) {
                table.remove(name);
                name = "lib/arm64-v8a/../escape.so";
            }

            table.put(name, spec);
            d.put("libraries", table);
            Files.writeString(badSource.resolve("component.json"), Json.write(d));
            reject(() -> GalleryArm64Libraries.selected(badSource));
        }
    }

    static void checkRejectedInputs(
            Path original,
            Map<String, byte[]> baseline,
            Map<String, byte[]> expected,
            Path component,
            Path source,
            Path out)
            throws Exception {
        Path fake = out.resolve("wrong-original.apk");

        Files.write(fake, new byte[] {1});
        reject(() ->
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", fake, baseline, component, source));
        Files.delete(fake);
        var wrongComponent = new TreeMap<>(expected);
        wrongComponent.remove(wrongComponent.keySet().iterator().next());
        Path wrongZip = out.resolve("wrong-component.zip");
        NativeCore.zip(wrongZip, wrongComponent, false);
        reject(() ->
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", original, baseline, wrongZip, source));
        Files.delete(wrongZip);

        for (String protectedEntry :
                List.of("classes.dex", "AndroidManifest.xml", "resources.arsc")) {
            var changed = new TreeMap<>(baseline);
            changed.put(protectedEntry, new byte[] {1});
            reject(() ->
                    GalleryArm64Libraries.add(
                            PROFILE, 30, "arm64-v8a", original, changed, component, source));
        }

        Path unsafe = out.resolve("unsafe.zip");

        try (var z = new java.util.zip.ZipOutputStream(Files.newOutputStream(unsafe))) {
            z.putNextEntry(new java.util.zip.ZipEntry("../unsafe"));
            z.write(1);
            z.closeEntry();
        }

        reject(() -> NativeCore.entries(unsafe));
        Files.delete(unsafe);
        Path duplicate = out.resolve("duplicate.zip");

        try (var z = new java.util.zip.ZipOutputStream(Files.newOutputStream(duplicate))) {
            for (String n : List.of("aa", "bb")) {
                z.putNextEntry(new java.util.zip.ZipEntry(n));
                z.write(1);
                z.closeEntry();
            }
        }

        byte[] duplicateBytes = Files.readAllBytes(duplicate);

        for (int i = 0; i < duplicateBytes.length - 1; i++) {
            if (duplicateBytes[i] == 'b' && duplicateBytes[i + 1] == 'b') {
                duplicateBytes[i] = 'a';
                duplicateBytes[i + 1] = 'a';
            }
        }

        Files.write(duplicate, duplicateBytes);
        reject(() -> NativeCore.entries(duplicate));
        Files.delete(duplicate);
    }

    static void checkArtifactsRoute(
            String[] args,
            Path root,
            Path source,
            Path out,
            Map<String, Object> config,
            Map<String, Object> signing,
            Map<String, byte[]> patched)
            throws Exception {
        Path proposal = Path.of(args[3]);
        Path recipe = proposal.resolve("profiles/" + PROFILE);
        Path privateCache = out.resolve("cache");

        Files.createDirectories(privateCache.resolve("sources"));
        var descriptor =
                Json.object(Json.parse(Files.readString(source.resolve("component.json"))));

        for (var entry : Json.obj(descriptor, "externalSources").entrySet()) {
            var pin = Json.object(entry.getValue());
            Path cached = privateCache.resolve("sources/" + Json.str(pin, "sha256"));

            if (!Files.exists(cached)) {
                Files.createLink(cached, source.resolve(entry.getKey()));
            }
        }

        var data = Json.object(Json.parse(Files.readString(recipe.resolve("profile.json"))));
        var originalSpec = Json.obj(data, "original").get("Gallery");
        var patch = Json.obj(data, "patches").get("Gallery");
        var selected =
                new ProfileRegistry.Profile(
                        PROFILE,
                        "private",
                        Map.of(
                                "android",
                                data.get("android"),
                                "original",
                                Map.of("Gallery", originalSpec),
                                "components",
                                Map.of(
                                        "gallery-arm64-libraries",
                                        "components/gallery-arm64-libraries"),
                                "patches",
                                Map.of("Gallery", patch),
                                "install",
                                List.of(Map.of("artifact", "patched:Gallery", "type", "apk"))),
                        "",
                        proposal);
        var metadata =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        recipe.resolve("patches/api30-gallery-arm64.json"))));
        metadata = new java.util.LinkedHashMap<>(metadata);
        metadata.put(
                "sourceSha256",
                FilesSupport.hash(recipe.resolve("patches/api30-gallery-arm64.json")));
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
                        out.resolve("normal-artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected Android input: " + logical);
                        },
                        System.out::println);
        Path produced = engine.build().get("patched:Gallery");
        var producedEntries = NativeCore.entries(produced);
        producedEntries.keySet().removeIf(DexOperations::signatureEntry);
        need(GalleryArm64Libraries.same(patched, producedEntries));
        System.out.println(
                "ACTUAL Artifacts component + Recipes validation/apply + build/sign route PASS:"
                        + " "
                        + produced
                        + " SHA="
                        + FilesSupport.hash(produced)
                        + " bytes="
                        + Files.size(produced));
    }
}
