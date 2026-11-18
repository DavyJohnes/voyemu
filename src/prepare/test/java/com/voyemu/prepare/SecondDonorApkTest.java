package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The real Artifacts APK branch puts the first OEM donor in classes.dex, the second donor with its
 * nested classes in classes2.dex and the own code in classes3.dex, and rejects a second donor that
 * repeats a class of the first or stands without one.
 */
public final class SecondDonorApkTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static String original(Path file, String entry, String... types) throws Exception {
        if (Files.exists(file)) {
            return FilesSupport.hash(file);
        }

        var classes = new java.util.ArrayList<org.jf.dexlib2.iface.ClassDef>();

        for (String type : types) {
            classes.add(DonorJarTest.type(type, classes.size()));
        }

        Storage.directory(file.getParent());
        NativeCore.zip(
                file,
                Map.of(
                        entry,
                        FrameworkDexMaterializer.serialize(
                                new ImmutableDexFile(Opcodes.forApi(30), classes))),
                false);

        return FilesSupport.hash(file);
    }

    private static Map<String, Object> donor(String original, String dex, String type) {
        return Map.of("original", original, "dex", dex, "classes", List.of(type));
    }

    private static Path build(Path root, Path project, String name, Map<String, Object> component)
            throws Exception {
        Path recipe = root.resolve("recipe-" + name), source = recipe.resolve("components/oem");
        Storage storage = Storage.resolve(root.resolve("home").toString(), Map.of(), "mac", root);
        Path originals = storage.originals("fixture");
        String first = original(originals.resolve("first.jar"), "classes.dex", "Lfixture/First;");
        String second =
                original(
                        originals.resolve("second.apk"),
                        "classes2.dex",
                        "Lfixture/First;",
                        "Lfixture/Second;",
                        "Lfixture/Second$Inner;",
                        "Lfixture/Unselected;");

        Storage.directory(source.resolve("src/fixture"));
        Files.writeString(
                source.resolve("AndroidManifest.xml"),
                "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\""
                        + " package=\"own.test.donors\"><application/></manifest>");
        Files.writeString(
                source.resolve("src/fixture/Own.java"),
                "package fixture; public class Own {public int value(){return 7;}}\n");
        Files.writeString(source.resolve("component.json"), Json.write(component) + "\n");
        Path key = root.resolve("synthetic.pk8"), certificate = root.resolve("synthetic.x509.der");

        if (!Files.exists(key)) {
            SigningIntermediateTest.syntheticSigning(root, key, certificate);
        }

        var data =
                Map.<String, Object>of(
                        "android",
                        Map.of("package", "system-images;android-30;default;arm64-v8a"),
                        "original",
                        Map.of(
                                "first",
                                Map.of("file", "first.jar", "sha256", first),
                                "second",
                                Map.of("file", "second.apk", "sha256", second)),
                        "components",
                        Map.of("oem", "components/oem"));
        var tools =
                new Toolchains(
                        storage,
                        Map.of("sdk", Map.of("buildTools", "36.1.0")),
                        project.resolve(".local/toolchains/java-sdk"),
                        key,
                        certificate,
                        project.resolve(
                                ".local/toolchains/java-sdk/platforms/android-34/android.jar"));

        return new Artifacts(
                        new Artifacts.PatchedProfile(
                                new ProfileRegistry.Profile("fixture", "test", data, ""), Map.of()),
                        storage,
                        tools,
                        recipe,
                        root.resolve("work-" + name),
                        path -> {
                            throw new AssertionError("no clean guest read required");
                        },
                        message -> {})
                .resolve("component:oem");
    }

    private static Map<String, Object> component(Map<String, Object> second, boolean first) {
        var result = new java.util.TreeMap<String, Object>();

        result.putAll(Map.of("type", "apk", "minSdk", 28, "targetSdk", 28, "sign", "platform"));
        result.put("secondDonorDex", second);

        if (first) {
            result.put("donorDex", donor("first", "classes.dex", "fixture.First"));
        }

        return result;
    }

    private static void rejected(
            Path root, Path project, String name, Map<String, Object> component, String code)
            throws Exception {
        try {
            build(root, project, name, component);
        } catch (Failure expected) {
            check(expected.code.equals(code), name + " failed with " + expected.code);

            return;
        }

        throw new AssertionError(name + " accepted");
    }

    public static void main(String[] args) throws Exception {
        Path project = Path.of(args[0]).toRealPath(),
                root =
                        Files.createTempDirectory(
                                project.resolve(".local/build/tmp"), "second-donor-apk-");

        try {
            Path apk =
                    build(
                            root,
                            project,
                            "layout",
                            component(donor("second", "classes2.dex", "fixture.Second"), true));
            var entries = NativeCore.entries(apk);

            check(
                    NativeCore.classes(DexOperations.dex(apk, "classes.dex", 30))
                            .equals(Set.of("Lfixture/First;")),
                    "first donor in classes.dex");
            check(
                    NativeCore.classes(DexOperations.dex(apk, "classes2.dex", 30))
                            .equals(Set.of("Lfixture/Second;", "Lfixture/Second$Inner;")),
                    "second donor with nested classes in classes2.dex");
            check(
                    NativeCore.classes(DexOperations.dex(apk, "classes3.dex", 30)).stream()
                            .filter(type -> type.startsWith("Lfixture/"))
                            .toList()
                            .equals(List.of("Lfixture/Own;")),
                    "own code in classes3.dex");
            check(!entries.containsKey("classes4.dex"), "no further DEX slot");
            rejected(
                    root,
                    project,
                    "collision",
                    component(donor("second", "classes2.dex", "fixture.First"), true),
                    "DEX_CLASS_COLLISION");
            rejected(
                    root,
                    project,
                    "alone",
                    component(donor("second", "classes2.dex", "fixture.Second"), false),
                    "DONOR_SPEC_INVALID");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println("Second OEM donor fills classes2.dex between first donor and own code");
    }
}
