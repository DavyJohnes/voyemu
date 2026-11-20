package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class FrameworkLibrariesTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1";

    static final String NAME = "api30-framework-libraries.json";

    static final class Target implements Recipes.Target {
        String profile = PROFILE, pin;
        int mutations;
        List<String> installed;

        @Override
        public String profile() {
            return profile;
        }

        @Override
        public int api() {
            return 30;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return null;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public String originalSha256() {
            return pin;
        }

        @Override
        public void addDex(String source) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path source, String name) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> names) {
            mutations++;
            installed = names;
        }
    }

    static void reject(Map<String, Object> metadata, Target target) throws Exception {
        boolean denied = false;

        try {
            Recipes.apply(NAME, metadata, target);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || target.mutations != 0) {
            throw new AssertionError("Invalid 6.11 recipe mutated target");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Path recipe =
                root.resolve("profiles/free-11-chinese-20250710-6.11.1/patches").resolve(NAME);
        var metadata = Json.object(Json.parse(Files.readString(recipe)));

        metadata.put("sourceSha256", FilesSupport.hash(recipe));
        Path originals = root.resolve(".local/profiles/free-11-chinese-20250710-6.11.1/originals");

        for (String name : List.of("QGSettingProvider", "SystemService", "SystemUI")) {
            var target = new Target();
            target.pin = FilesSupport.hash(originals.resolve(name + ".apk"));
            Recipes.apply(NAME, metadata, target);

            if (target.mutations != 1 || !List.of("QGAPI").equals(target.installed)) {
                throw new AssertionError("QGAPI recipe failed for " + name);
            }
        }

        var target = new Target();
        target.pin = "0".repeat(64);
        reject(metadata, target);
        var changed = new HashMap<String, Object>(metadata);
        changed.put(
                "calls",
                List.of(
                        Map.of(
                                "operation",
                                "useLibraries",
                                "args",
                                List.of(List.of("QGAPI", "QGVehicle")))));
        target = new Target();
        target.pin = FilesSupport.hash(originals.resolve("SystemUI.apk"));
        reject(changed, target);
        System.out.println("Free A11 6.11.1 framework library recipe checks passed");
    }
}
