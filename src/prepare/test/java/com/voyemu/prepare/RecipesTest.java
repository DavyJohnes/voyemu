package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class RecipesTest {
    static final class FileTarget implements Recipes.Target {
        final Path work, output;
        final Map<String, Path> uses;

        FileTarget(Path work, Path output, Map<String, Path> uses) {
            this.work = work;
            this.output = output;
            this.uses = uses;
        }

        @Override
        public int api() {
            return 28;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return work;
        }

        @Override
        public Path output() {
            return output;
        }

        @Override
        public Map<String, Path> uses() {
            return uses;
        }

        @Override
        public void addDex(String source) {
            throw new AssertionError("Unexpected DEX generation");
        }

        @Override
        public void putEntry(Path source, String name) {
            throw new AssertionError("Unexpected APK entry");
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError("Unexpected APK removal");
        }

        @Override
        public void useLibraries(List<String> names) {
            throw new AssertionError("Unexpected manifest edit");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("voyemu-native-recipe-test-").toRealPath();

        try {
            Path output = root.resolve("output");
            Files.writeString(output, "existing\n");
            FileTarget target = new FileTarget(root, output, Map.of());
            checkSelinuxRecipes(root, output);
            Map<String, Object> index =
                    ProfileRegistry.patchMetadata(
                            new ProfileRegistry(
                                            Path.of(
                                                            System.getProperty(
                                                                    "voyemu.project.root",
                                                                    System.getProperty("user.dir")))
                                                    .toRealPath())
                                    .get("free-09-chinese-20260106-6.0.3"));
            checkLineEdits(output, target, index);
            Map<String, Object> shutdown = Json.object(index.get("shutdown-router-a9.json"));
            Files.writeString(output, "changed\n");

            try {
                Recipes.apply("shutdown-router-a9.json", shutdown, target);
                throw new AssertionError("Changed shutdown baseline accepted");
            } catch (Failure failure) {
                StorageJsonTest.check(failure.code.equals("PATCH_ASSERTION_FAILED"));
            }

            StorageJsonTest.check(Files.readString(output).equals("changed\n"));
            checkShutdownInstall();

            if (args.length == 1) {
                checkShutdownOutput(Path.of(args[0]), output, target, shutdown);
            }
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println("Static recipe data, exact newline semantics and policy guards passed");
    }

    static void checkSelinuxRecipes(Path root, Path output) throws Exception {
        Files.writeString(output, "(type a)\n(type b)\n(typepermissive su)\n");
        Path plat = root.resolve("own-plat-policy");
        Files.writeString(plat, "(allow su a (file (read)))\n(allow su absent (file (read)))\n");
        Path vendor = root.resolve("own-vendor-policy");
        Files.writeString(
                vendor,
                """
                (allow su_28_0 b_28_0 (dir (search)))
                (allow su_28_0 self (process (execmem)))
                (allow su_28_0 vendor_only (file (read)))
                """);
        FileTarget policy =
                new FileTarget(
                        root,
                        output,
                        Map.of("original:PlatSepolicy", plat, "original:VendorSepolicy", vendor));
        Recipes.apply(
                "selinux-bounded-su-a9.json",
                Map.of(
                        "constants",
                        Map.of("MARKER", "; own bounded", "PERMISSIVE", "(typepermissive su)"),
                        "calls",
                        List.of()),
                policy);
        String bounded = Files.readString(output);
        StorageJsonTest.check(
                !bounded.contains("typepermissive")
                        && bounded.contains(
                                """
                                (allow su a (file (read)))
                                (allow su b (dir (search)))
                                (allow su self (process (execmem)))
                                """)
                        && !bounded.contains("allow su absent")
                        && !bounded.contains("vendor_only")
                        && !bounded.contains("_28_0"));
    }

    static void checkLineEdits(Path output, FileTarget target, Map<String, Object> index)
            throws Exception {
        Map<String, Object> declared = Json.object(index.get("disable-rild-a9.json"));
        Map<String, Object> call = Json.object(Json.list(declared.get("calls")).get(0));

        StorageJsonTest.check(Json.str(call, "operation").equals("editLines"));
        String original = "fixture service\nfixture class\nfixture user\n";
        String expected = "fixture service\nfixture class\n    disabled\nfixture user\n";
        Files.writeString(output, original);
        Map<String, Object> own =
                Map.of(
                        "constants",
                        Map.of(),
                        "calls",
                        List.of(
                                Map.of(
                                        "operation",
                                        "editLines",
                                        "args",
                                        List.of(
                                                "output",
                                                Json.list(Json.list(call.get("args")).get(1)),
                                                NativeCore.hash(
                                                        expected.getBytes(
                                                                java.nio.charset.StandardCharsets
                                                                        .UTF_8))))));
        Recipes.apply("synthetic-line-edit.json", own, target);
        StorageJsonTest.check(Files.readString(output).equals(expected));
        Files.writeString(output, original);

        try {
            Recipes.apply("disable-rild-a9.json", declared, target);
            throw new AssertionError("Unpinned OEM fixture accepted");
        } catch (Failure failure) {
            StorageJsonTest.check(failure.code.equals("SOURCE_EDIT_HASH_MISMATCH"));
        }

        try {
            Recipes.apply(
                    "synthetic-literal.json",
                    Map.of(
                            "constants",
                            Map.of(),
                            "calls",
                            List.of(
                                    Map.of(
                                            "operation",
                                            "replace",
                                            "args",
                                            List.of("output", "fixture", "other", 1)))),
                    target);
            throw new AssertionError("Literal replacement accepted");
        } catch (Failure failure) {
            StorageJsonTest.check(failure.code.equals("PATCH_OPERATION_UNSUPPORTED"));
        }
    }

    static void checkShutdownInstall() throws Exception {
        Map<String, Object> definition =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        Path.of(
                                                        System.getProperty(
                                                                "voyemu.project.root",
                                                                System.getProperty("user.dir")))
                                                .resolve(
                                                        "profiles/free-09-chinese-20260106-6.0.3/profile.json"))));
        Map<String, Object> shutdownNode =
                Json.object(Json.obj(definition, "patches").get("shutdown-router-a9"));

        StorageJsonTest.check(
                Json.str(shutdownNode, "input")
                        .equals("android:/vendor/etc/init/hw/init.ranchu.rc"));
        long installs =
                Json.list(definition.get("install")).stream()
                        .map(Json::object)
                        .filter(item ->
                                "patched:shutdown-router-a9".equals(item.get("artifact"))
                                        && "/vendor/etc/init/hw/init.ranchu.rc"
                                                .equals(item.get("target"))
                                        && "0644".equals(item.get("mode")))
                        .count();
        StorageJsonTest.check(installs == 1);
    }

    static void checkShutdownOutput(
            Path baselineFile, Path output, FileTarget target, Map<String, Object> shutdown)
            throws Exception {
        byte[] baseline = Files.readAllBytes(baselineFile);

        Files.write(output, baseline);
        Recipes.apply("shutdown-router-a9.json", shutdown, target);
        byte[] candidate = Files.readAllBytes(output);
        StorageJsonTest.check(
                java.util.Arrays.equals(
                        candidate,
                        (new String(baseline, java.nio.charset.StandardCharsets.UTF_8)
                                        + "\n"
                                        + "on shutdown\n"
                                        + "    umount /data/vendor/var/run/netns/router\n")
                                .getBytes(java.nio.charset.StandardCharsets.UTF_8)));
        StorageJsonTest.check(
                NativeCore.hash(candidate)
                        .equals(
                                "b49dd4349af7d4f0a5b961679075421e68faa28a4a4410e4b407636020aafba4"));

        try {
            Recipes.apply("shutdown-router-a9.json", shutdown, target);
            throw new AssertionError("Repeated shutdown patch accepted");
        } catch (Failure failure) {
            StorageJsonTest.check(failure.code.equals("PATCH_ASSERTION_FAILED"));
        }

        StorageJsonTest.check(java.util.Arrays.equals(Files.readAllBytes(output), candidate));
        System.out.println(
                "Exact shutdown baseline, output pin, only ordinary umount and duplicate"
                        + " refusal passed");
    }
}
