package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Exercises the real Artifacts type=jar branch, not only its donor primitive. */
public final class DonorJarBranchTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException("ROOT ANDROID_JAR");
        }

        Path project = Path.of(args[0]).toRealPath(),
                platform = Path.of(args[1]).toRealPath(),
                root =
                        Files.createTempDirectory(
                                project.resolve(".local/build/tmp"), "donor-jar-branch-");

        try {
            Storage storage =
                    Storage.resolve(root.resolve("home").toString(), Map.of(), "mac", root);
            Path original = storage.originals("fixture").resolve("owner.jar");
            Storage.directory(original.getParent());
            byte[] dex =
                    FrameworkDexMaterializer.serialize(
                            new ImmutableDexFile(
                                    Opcodes.forApi(30),
                                    List.of(DonorJarTest.type("Lfixture/Donor;", 42))));
            NativeCore.zip(original, Map.of("classes.dex", dex), false);
            String pin = FilesSupport.hash(original);
            Path recipe = root.resolve("recipe"), component = recipe.resolve("components/library");
            Storage.directory(component);
            Files.writeString(
                    component.resolve("component.json"),
                    Json.write(
                                    Map.of(
                                            "type",
                                            "jar",
                                            "minSdk",
                                            30,
                                            "output",
                                            "library.jar",
                                            "donorDex",
                                            Map.of(
                                                    "original",
                                                    "frameworkBase",
                                                    "sha256",
                                                    pin,
                                                    "dex",
                                                    "classes.dex",
                                                    "classes",
                                                    List.of("fixture.Donor"))))
                            + "\n");
            var data =
                    Map.<String, Object>of(
                            "android",
                            Map.of("package", "system-images;android-30;default;arm64-v8a"),
                            "original",
                            Map.of("frameworkBase", Map.of("file", "owner.jar", "sha256", pin)),
                            "components",
                            Map.of("library", "components/library"));
            var profile = new ProfileRegistry.Profile("fixture", "test", data, "");
            var tools = new Toolchains(storage, Map.of(), root, null, null, platform);
            var artifacts =
                    new Artifacts(
                            new Artifacts.PatchedProfile(profile, Map.of()),
                            storage,
                            tools,
                            recipe,
                            root.resolve("work"),
                            path -> {
                                throw new AssertionError("no clean guest read required");
                            },
                            message -> {});
            Path output = artifacts.resolve("component:library");

            if (!NativeCore.entries(output).keySet().equals(Set.of("classes.dex"))) {
                throw new AssertionError("real type=jar branch did not produce donor-only library");
            }

            checkIndependentBluetooth(root, storage, tools, recipe, original, pin);
            checkLegacyOwnJar(root, storage, tools, recipe);
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println("Real Artifacts donor-only JAR branch passed; guest_started=false");
    }

    private static void checkIndependentBluetooth(
            Path root, Storage storage, Toolchains tools, Path recipe, Path original, String pin)
            throws Exception {
        Path bluetooth = recipe.resolve("components/bluetooth");

        Storage.directory(bluetooth.resolve("src/fixture"));
        Storage.directory(bluetooth.resolve("src/stub-cls"));
        Files.writeString(
                bluetooth.resolve("src/stub-cls/Declaration.java"),
                "package fixture.declarations; public class Declaration {}\n");
        Files.writeString(
                bluetooth.resolve("component.json"),
                Json.write(
                                Map.of(
                                        "type",
                                        "jar",
                                        "minSdk",
                                        28,
                                        "output",
                                        "Ivt8BluetoothAdapter.jar",
                                        "compileOnly",
                                        "src/stub-cls"))
                        + "\n");
        Files.writeString(
                bluetooth.resolve("src/fixture/Adapter.java"),
                "package fixture; public class Adapter {public int value(){return 9;}}\n");
        var localData = new java.util.LinkedHashMap<String, Object>();
        localData.put("id", "independent-local-copy");
        localData.put(
                "capture",
                Map.of(
                        "profile",
                        "independent-local-copy",
                        "referenceProfile",
                        TestProfiles.SYNTHETIC));
        localData.put("android", Map.of("package", "system-images;android-28;default;arm64-v8a"));
        localData.put("components", Map.of("bluetooth", "components/bluetooth"));
        var localProfile =
                new ProfileRegistry.Profile("independent-local-copy", "test", localData, "");
        Path sharedOriginal = storage.originals(TestProfiles.SYNTHETIC).resolve("owner.jar");
        Storage.directory(sharedOriginal.getParent());
        Files.copy(original, sharedOriginal);
        localData.put(
                "original", Map.of("frameworkBase", Map.of("file", "owner.jar", "sha256", pin)));

        if (!Inputs.originals(localProfile, storage).equals(sharedOriginal.getParent())
                || !Inputs.check(localProfile, storage).ok()) {
            throw new AssertionError("Independent source profile failed immutable OEM input reuse");
        }

        if (!localProfile.id().equals("independent-local-copy")
                || !localProfile.referenceProfile().equals(TestProfiles.SYNTHETIC)) {
            throw new AssertionError("Logical source identity replaced by adapter family");
        }

        var localArtifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(localProfile, Map.of()),
                        storage,
                        tools,
                        recipe,
                        root.resolve("local-work"),
                        path -> {
                            throw new AssertionError("No guest read for Bluetooth compilation");
                        },
                        message -> {});

        if (!NativeCore.entries(localArtifacts.resolve("component:bluetooth"))
                .keySet()
                .equals(Set.of("classes.dex"))) {
            throw new AssertionError("Independent A9 profile omitted Bluetooth component");
        }

        if (!localArtifacts.resolve("original:frameworkBase").equals(sharedOriginal)) {
            throw new AssertionError("Artifact resolver duplicated OEM input");
        }

        localData.put(
                "capture",
                Map.of("profile", "foreign-copy", "referenceProfile", TestProfiles.SYNTHETIC));
        boolean refused = false;

        try {
            localProfile.referenceProfile();
        } catch (Failure expected) {
            refused = true;
        }

        if (!refused) {
            throw new AssertionError("Foreign logical reference binding accepted");
        }
    }

    private static void checkLegacyOwnJar(Path root, Storage storage, Toolchains tools, Path recipe)
            throws Exception {
        Path own = recipe.resolve("components/own");

        Storage.directory(own.resolve("src/fixture"));
        Files.writeString(
                own.resolve("component.json"),
                Json.write(Map.of("type", "jar", "minSdk", 28, "output", "own.jar")) + "\n");
        Files.writeString(
                own.resolve("src/fixture/Own.java"),
                "package fixture; public class Own {public int value(){return 7;}}\n");
        var legacyData =
                Map.<String, Object>of(
                        "android",
                        Map.of("package", "system-images;android-28;default;arm64-v8a"),
                        "components",
                        Map.of("own", "components/own"));
        var legacyProfile = new ProfileRegistry.Profile("legacy", "test", legacyData, "");
        var legacy =
                new Artifacts(
                        new Artifacts.PatchedProfile(legacyProfile, Map.of()),
                        storage,
                        tools,
                        recipe,
                        root.resolve("legacy-work"),
                        path -> {
                            throw new AssertionError("no guest read");
                        },
                        message -> {});

        if (!NativeCore.entries(legacy.resolve("component:own"))
                .keySet()
                .equals(Set.of("classes.dex"))) {
            throw new AssertionError("legacy API28 own-source JAR branch changed");
        }
    }
}
