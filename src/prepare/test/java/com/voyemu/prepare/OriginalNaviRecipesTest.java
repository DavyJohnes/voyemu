package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class OriginalNaviRecipesTest {
    static final class Target implements Recipes.Target {
        String pin;
        boolean derived = true;
        int validate, apply;

        Target(String p) {
            pin = p;
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
        public String profile() {
            return "free-11-chinese-20260731-7.3.1";
        }

        @Override
        public Path work() {
            return Path.of(".");
        }

        @Override
        public Path output() {
            return Path.of(".");
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
        public boolean derivedInput() {
            return derived;
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

        @Override
        public void guardOriginalNaviEgl(boolean b) {
            if (b) {
                apply++;
            } else {
                validate++;
            }
        }

        @Override
        public void guardOriginalNaviLibraries(boolean b) {
            if (b) {
                apply++;
            } else {
                validate++;
            }
        }
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        for (String name : List.of("original-navi-egl.json", "original-navi-libraries.json")) {
            Path p = Path.of("profiles/free-11-chinese-20260731-7.3.1/patches", name);
            Map<String, Object> m = new HashMap<>(Json.object(Json.parse(Files.readString(p))));
            m.put("sourceSha256", "a".repeat(64));
            check(Json.list(Json.obj(m, "constants").get("original_sha256_allowlist")).isEmpty());
            Target t = new Target("0".repeat(64));
            Recipes.validateApi30(name, m, t);
            check(t.validate == 1 && t.apply == 0);
            Recipes.apply(name, m, t);
            check(t.validate == 2 && t.apply == 1);
            t.derived = false;

            try {
                Recipes.apply(name, m, t);
                throw new AssertionError("unpinned original input accepted");
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }

        System.out.println("PASS inactive recipe dispatch and unpinned original input negatives");
    }
}
