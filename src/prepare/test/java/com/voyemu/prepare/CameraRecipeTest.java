package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class CameraRecipeTest {
    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static Map<String, Object> contract(String backend) {
        return new LinkedHashMap<>(
                Map.of(
                        "schema",
                        1,
                        "profile",
                        TestProfiles.SYNTHETIC,
                        "backend",
                        backend,
                        "physicalDms",
                        false,
                        "physicalRpm",
                        false));
    }

    private static final class Target implements Recipes.Target {
        final Path root, work;
        final DexClasses classes;
        int mutations;
        int api = 28;

        Target(Path root, Path work) throws Exception {
            this.root = root;
            this.work = work;
            // A DEX without the OEM camera classes.
            var unrelated =
                    new org.jf.dexlib2.immutable.ImmutableClassDef(
                            "Lcom/voyemu/fixture/Unrelated;",
                            1,
                            "Ljava/lang/Object;",
                            null,
                            null,
                            null,
                            null,
                            null);
            Map<String, byte[]> archive =
                    new java.util.TreeMap<>(
                            Map.of(
                                    "classes.dex",
                                    FrameworkDexMaterializer.serialize(
                                            new org.jf.dexlib2.immutable.ImmutableDexFile(
                                                    org.jf.dexlib2.Opcodes.forApi(28),
                                                    List.of(unrelated)))));
            classes = new DexClasses(archive, List.of("classes.dex"), 28);
        }

        @Override
        public int api() {
            return api;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path stepRoot() {
            return root;
        }

        @Override
        public Path work() {
            return work;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public DexClasses dex() {
            return classes;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String source) {
            mutations++;
        }

        @Override
        public void putEntry(Path source, String name) {
            mutations++;
        }

        @Override
        public void removeEntry(String name) {
            mutations++;
        }

        @Override
        public void useLibraries(List<String> names) {
            mutations++;
        }
    }

    private static void refused(Object input) {
        boolean rejected = false;

        try {
            CameraRecipe.validate(input);
        } catch (Failure error) {
            rejected = error.code.equals("CAMERA_CONTRACT");
        }

        check(rejected, "Invalid camera contract accepted: " + input);
    }

    public static void main(String[] args) throws Exception {
        CameraRecipe.validate(contract("none"));
        CameraRecipe.validate(contract("android-back-camera"));
        refused(null);
        refused(List.of());

        for (String key : List.of("schema", "profile", "backend", "physicalDms", "physicalRpm")) {
            Map<String, Object> invalid = contract("none");
            invalid.remove(key);
            refused(invalid);
        }

        Map<String, Object> invalid = contract("none");
        invalid.put("extra", false);
        refused(invalid);
        invalid = contract("none");
        invalid.put("physicalDms", true);
        refused(invalid);
        invalid = contract("none");
        invalid.put("physicalRpm", true);
        refused(invalid);
        invalid = contract("none");
        invalid.put("schema", "1");
        refused(invalid);
        invalid = contract("none");
        invalid.put("backend", "physical-camera");
        refused(invalid);
        Path project =
                Path.of(System.getProperty("voyemu.project.root", System.getProperty("user.dir")))
                        .toRealPath();
        check(
                Files.isDirectory(project.resolve("src/prepare")),
                "Explicit Voyemu project root required");
        Path parent = project.resolve(".local/build/tmp");
        Storage.directory(parent);
        Path stage = Files.createTempDirectory(parent, "camera-recipe-test-");

        try {
            Path stepRoot = stage.resolve("patches"), work = stage.resolve("work");
            Files.createDirectories(stepRoot.resolve("camera-capabilities"));
            Files.createDirectories(work);
            Path asset = stepRoot.resolve("camera-capabilities/voyemu-camera-capabilities.json");
            Files.writeString(asset, Json.write(contract("none")));
            Target target = new Target(stepRoot, work);
            CameraRecipe.apply(target);

            try (var entries = Files.list(work)) {
                check(
                        target.mutations == 0 && entries.findAny().isEmpty(),
                        "none mutated OEM work");
            }

            Files.writeString(asset, Json.write(contract("android-back-camera")));
            boolean rejected = false;

            try {
                CameraRecipe.apply(target);
            } catch (Failure error) {
                rejected = error.code.equals("DEX_EDIT_ANCHOR");
            }

            check(rejected && target.mutations == 0, "Absent OEM camera classes mutated target");

            target.mutations = 0;
            target.api = 30;
            rejected = false;

            try {
                CameraRecipe.apply(target);
            } catch (Failure error) {
                rejected = error.code.equals("CAMERA_CONTRACT");
            }

            check(rejected && target.mutations == 0, "Wrong API accepted");
            System.out.println("camera contract, none and pre-mutation class guards passed");
        } finally {
            FilesSupport.removeOwnedTree(stage);
        }
    }
}
