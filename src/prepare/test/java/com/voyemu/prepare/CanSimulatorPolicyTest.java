package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

public final class CanSimulatorPolicyTest {
    static String recipe(String name) {
        try {
            return NativeCore.hash(
                    java.nio.file.Files.readAllBytes(
                            java.nio.file.Path.of(System.getProperty("user.dir"))
                                    .resolve(
                                            "profiles/"
                                                    + TestProfiles.declaring(
                                                            java.nio.file.Path.of(
                                                                    System.getProperty("user.dir")),
                                                            "can-simulator-policy-a9")
                                                    + "/patches/"
                                                    + name)));
        } catch (Exception error) {
            throw new IllegalStateException(error);
        }
    }

    static final class Target implements Recipes.Target {
        final Path file, plat, vendor;
        String profile = TestProfiles.SYNTHETIC;
        int api = 28;

        Target(Path f, Path p, Path v) {
            file = f;
            plat = p;
            vendor = v;
        }

        @Override
        public String profile() {
            return profile;
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
        public Path work() {
            return file.getParent();
        }

        @Override
        public Path output() {
            return file;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of("original:PlatSepolicy", plat, "original:VendorSepolicy", vendor);
        }

        @Override
        public void addDex(String s) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String s) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String s) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> s) {
            throw new AssertionError();
        }
    }

    static Map<String, Object> metadata() {
        return Map.of(
                "api",
                28,
                "sourceSha256",
                recipe("can-simulator-policy-a9.json"),
                "constants",
                Map.of("KIND", "can-simulator-policy"),
                "calls",
                List.of());
    }

    static void denied(Target t) throws Exception {
        byte[] before = Files.readAllBytes(t.file);

        try {
            Recipes.apply("can-simulator-policy-a9.json", metadata(), t);
            throw new AssertionError();
        } catch (Failure e) {
            if (!Arrays.equals(before, Files.readAllBytes(t.file))) {
                throw new AssertionError();
            }
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Files.createTempDirectory("can-policy-test-"), file = root.resolve("policy");

        Files.copy(Path.of(a[0]), file);
        Target t = new Target(file, Path.of(a[1]), Path.of(a[2]));
        Recipes.apply(
                "selinux-canbus-a9.json",
                Map.of("constants", Map.of("MARKER", "; voyemu A9 canbus"), "calls", List.of()),
                t);
        Recipes.apply(
                "selinux-bounded-su-a9.json",
                Map.of(
                        "constants",
                        Map.of(
                                "MARKER",
                                "; voyemu A9 bounded su",
                                "PERMISSIVE",
                                "(typepermissive su)"),
                        "calls",
                        List.of()),
                t);
        byte[] base = Files.readAllBytes(file);

        if (!NativeCore.hash(base)
                .equals("3df9140132bf65a7e6e59d8fec735595feee3ed3b32d57d7f6b4a10b82cb903e")) {
            throw new AssertionError("baseline mismatch");
        }

        t.profile = TestProfiles.SYNTHETIC;
        t.api = 30;
        denied(t);
        t.api = 28;
        Recipes.apply("can-simulator-policy-a9.json", metadata(), t);

        if (!NativeCore.hash(Files.readAllBytes(file))
                .equals("ea017c277007b5ba8401ab214b2d607d72ddd39b7a303db05eb5281cb405fd22")) {
            throw new AssertionError("C++ Java policy mismatch");
        }

        denied(t);
        var early =
                Map.<String, Object>of(
                        "sourceSha256",
                        recipe("can-early-policy-a9.json"),
                        "constants",
                        Map.of("KIND", "can-early-policy"),
                        "calls",
                        List.of());
        Recipes.apply("can-early-policy-a9.json", early, t);

        if (!NativeCore.hash(Files.readAllBytes(file))
                .equals("2879712e7a6a12f262ab346fbe61b7f5b992e652e10d93edf8eb44a6c7b84a77")) {
            throw new AssertionError("early policy parity");
        }

        try {
            Recipes.apply("can-early-policy-a9.json", early, t);
            throw new AssertionError();
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        Files.delete(file);
        Files.delete(root);
        System.out.println("Java simulator policy guards passed");
    }
}
