package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

public final class CanEarlyPropertyTest {
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

    static class T implements Recipes.Target {
        Path file;
        String profile = "free-09-chinese-20260106-6.0.3";
        int api = 28;

        T(Path f) {
            file = f;
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
            return Map.of();
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
        public void useLibraries(List<String> x) {
            throw new AssertionError();
        }
    }

    static Map<String, Object> m() {
        return Map.of(
                "sourceSha256",
                recipe("can-early-property-contexts-a9.json"),
                "constants",
                Map.of("KIND", "can-early-property-contexts"),
                "calls",
                List.of());
    }

    static void denied(T t) throws Exception {
        byte[] before = Files.readAllBytes(t.file);

        try {
            Recipes.apply("can-early-property-contexts-a9.json", m(), t);
            throw new AssertionError();
        } catch (Failure e) {
            if (!Arrays.equals(before, Files.readAllBytes(t.file))) {
                throw new AssertionError();
            }
        }
    }

    public static void main(String[] a) throws Exception {
        Path d = Files.createTempDirectory("can-property-"), f = d.resolve("file");

        Files.copy(Path.of(a[0]), f);
        T t = new T(f);
        var entries =
                List.of(
                        "persist.app. u:object_r:system_prop:s0",
                        "persist.sys.qg. u:object_r:system_prop:s0",
                        "persist.clusterCardInfo u:object_r:system_prop:s0",
                        "persist.media.metrics.enabled u:object_r:system_prop:s0",
                        "ignoreActivation u:object_r:system_prop:s0");
        Recipes.apply(
                "selinux-property-contexts-a9.json",
                Map.of("constants", Map.of("ENTRIES", entries), "calls", List.of()),
                t);
        t.profile = "other";
        denied(t);
        t.profile = "free-09-chinese-20260106-6.0.3";
        t.api = 30;
        denied(t);
        t.api = 28;
        Recipes.apply("can-early-property-contexts-a9.json", m(), t);

        if (!NativeCore.hash(Files.readAllBytes(f))
                .equals("bd2a3eafde0f3d98966d590dcc70047f9fa38e9812f3660a0326182e0f43540d")) {
            throw new AssertionError();
        }

        denied(t);
        Files.delete(f);
        Files.delete(d);
        System.out.println("Java early property guards passed");
    }
}
