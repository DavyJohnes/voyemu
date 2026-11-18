package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class MediaRecipesTest {
    static final String PROFILE_ID = "free-11-chinese-20250710-6.11.1";
    static final Path PROFILE = Path.of("profiles/" + PROFILE_ID + "/patches");
    static final Path ORIGINAL =
            Path.of(".local/profiles/free-11-chinese-20250710-6.11.1/originals");

    static final class Target implements Recipes.Target {
        final Map<String, byte[]> entries = new HashMap<>();
        String original;
        int mutations;

        @Override
        public String profile() {
            return PROFILE_ID;
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
            return original;
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
            mutations++;
            entries.remove(n);
        }

        @Override
        public void useLibraries(List<String> n) {
            mutations++;
        }

        @Override
        public void verifyPinnedEntry(String n, String pin) throws Exception {
            Artifacts.verifyPinnedNativeEntry(entries, n, pin);
        }
    }

    static Target target(Path apk) throws Exception {
        var t = new Target();

        t.original = FilesSupport.hash(apk);

        try (var zip = new ZipFile(apk.toFile())) {
            for (var e : Collections.list(zip.entries())) {
                if (!e.isDirectory() && e.getName().startsWith("lib/armeabi-v7a/")) {
                    t.entries.put(e.getName(), zip.getInputStream(e).readAllBytes());
                }
            }
        }

        return t;
    }

    static void reject(String name, Map<String, Object> metadata, Target t) throws Exception {
        boolean denied = false;

        try {
            Recipes.apply(name, metadata, t);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || t.mutations != 0) {
            throw new AssertionError("Invalid recipe mutated before validation: " + name);
        }
    }

    static void test(String name, String apk) throws Exception {
        var metadata =
                new HashMap<String, Object>(
                        Json.object(Json.parse(Files.readString(PROFILE.resolve(name)))));

        metadata.put("sourceSha256", FilesSupport.hash(PROFILE.resolve(name)));
        Path path = ORIGINAL.resolve(apk);
        var good = target(path);
        int initial = good.entries.size();
        var calls = Json.list(metadata.get("calls"));
        long removals =
                calls.stream()
                        .map(Json::object)
                        .filter(c -> "removePinnedEntry".equals(c.get("operation")))
                        .count();
        Recipes.apply(name, metadata, good);

        if (good.entries.size() != initial - removals || good.mutations != calls.size()) {
            throw new AssertionError("Unexpected recipe effects");
        }

        var wrongApk = target(path);
        wrongApk.original = "0".repeat(64);
        reject(name, metadata, wrongApk);
        var badEntry = target(path);
        var removal =
                calls.stream()
                        .map(Json::object)
                        .filter(c -> "removePinnedEntry".equals(c.get("operation")))
                        .findFirst()
                        .orElseThrow();
        String entry = (String) Json.list(removal.get("args")).get(0);
        badEntry.entries.put(entry, new byte[] {0});
        reject(name, metadata, badEntry);
        var missing = target(path);
        missing.entries.remove(entry);
        reject(name, metadata, missing);
        var duplicate = new HashMap<String, Object>(metadata);
        var extra = new ArrayList<Object>(calls);
        extra.add(removal);
        duplicate.put("calls", extra);
        reject(name, duplicate, target(path));
    }

    public static void main(String[] args) throws Exception {
        test("music-optional-arm32.json", "MusicConcate.apk");
        test("api30-media-runtime.json", "QGMedia.apk");
    }
}
