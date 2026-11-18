package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class CanSimulatorContextsTest {
    static String recipe(String name) {
        try {
            return NativeCore.hash(
                    java.nio.file.Files.readAllBytes(
                            java.nio.file.Path.of(System.getProperty("user.dir"))
                                    .resolve(
                                            "profiles/free-09-chinese-20260106-6.0.3/patches/"
                                                    + name)));
        } catch (Exception error) {
            throw new IllegalStateException(error);
        }
    }

    static final String PIN = recipe("can-simulator-contexts-a9.json");

    static final class Target implements Recipes.Target {
        final Path file;
        String profile = "free-09-chinese-20260106-6.0.3";
        int api = 28;

        Target(Path file) {
            this.file = file;
        }

        @Override
        public int api() {
            return api;
        }

        @Override
        public String profile() {
            return profile;
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
            return Map.of();
        }

        @Override
        public void addDex(String s) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String n) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String n) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> n) {
            throw new AssertionError();
        }
    }

    static Map<String, Object> metadata(String pin) {
        return Map.of(
                "api",
                28,
                "calls",
                List.of(),
                "constants",
                Map.of("KIND", "can-simulator-file-contexts"),
                "sourceSha256",
                pin);
    }

    static void denied(Target target, Map<String, Object> metadata) throws Exception {
        byte[] before = Files.readAllBytes(target.file);

        try {
            Recipes.apply("can-simulator-contexts-a9.json", metadata, target);
            throw new AssertionError("unsupported context accepted");
        } catch (Failure expected) {
            if (!java.util.Arrays.equals(before, Files.readAllBytes(target.file))) {
                throw new AssertionError("failure changed source");
            }
        }
    }

    public static void main(String[] args) throws Exception {
        Path source = Path.of(args[0]),
                dir = Files.createTempDirectory("can-context-test-"),
                file = dir.resolve("contexts");
        byte[] stock = Files.readAllBytes(source);

        Files.write(file, stock);
        Target target = new Target(file);
        target.profile = "other";
        denied(target, metadata(PIN));
        target.profile = "free-09-chinese-20260106-6.0.3";
        target.api = 30;
        denied(target, metadata(PIN));
        target.api = 28;
        Files.writeString(file, "changed");
        denied(target, metadata(PIN));
        Files.write(file, stock);
        Recipes.apply("can-simulator-contexts-a9.json", metadata(PIN), target);
        String result = Files.readString(file);

        if (!NativeCore.hash(result.getBytes(java.nio.charset.StandardCharsets.UTF_8))
                .equals("b140f7f850f0279e8935053b948784708c9c62de4aeb59019a7c07ef0d9f5813")) {
            throw new AssertionError("C++/Java output differs");
        }

        denied(target, metadata(PIN));
        var early =
                Map.<String, Object>of(
                        "sourceSha256",
                        recipe("can-early-file-contexts-a9.json"),
                        "constants",
                        Map.of("KIND", "can-early-file-contexts"),
                        "calls",
                        List.of());
        Recipes.apply("can-early-file-contexts-a9.json", early, target);

        if (!NativeCore.hash(Files.readAllBytes(file))
                .equals("8bd8062c3b8108bdc34573f78d403691e747298a986a3464ace94721b02ad1d7")) {
            throw new AssertionError("early C++/Java contexts differ");
        }

        try {
            Recipes.apply("can-early-file-contexts-a9.json", early, target);
            throw new AssertionError("reapply accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        Files.delete(file);
        Files.delete(dir);
        System.out.println("Java simulator context guards passed");
    }
}
