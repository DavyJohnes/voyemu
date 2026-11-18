package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class OriginalNaviIdentityRecipesTest {
    static final class Target implements Recipes.Target {
        String pin;
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
        public void guardOriginalNaviVin(boolean b) {
            if (b) {
                apply++;
            } else {
                validate++;
            }
        }

        @Override
        public void guardOriginalNaviVehicle(boolean b) {
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

    static void reject(String n, Map<String, Object> m, Target t) throws Exception {
        try {
            Recipes.apply(n, m, t);
            throw new AssertionError("invalid recipe accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    public static void main(String[] args) throws Exception {
        for (String name : List.of("original-navi-vin.json", "original-navi-vehicle.json")) {
            Path p = Path.of("profiles/free-11-chinese-20260731-7.3.1/patches", name);
            Map<String, Object> m = new HashMap<>(Json.object(Json.parse(Files.readString(p))));
            m.put("sourceSha256", "a".repeat(64));
            String pin =
                    (String)
                            Json.list(Json.obj(m, "constants").get("original_sha256_allowlist"))
                                    .get(0);
            Target t = new Target(pin);
            Recipes.validateApi30(name, m, t);
            check(t.validate == 1 && t.apply == 0);
            Recipes.apply(name, m, t);
            check(t.validate == 2 && t.apply == 1);
            t.pin = "0".repeat(64);
            reject(name, m, t);
            t.pin = pin;
            Map<String, Object> bad = new HashMap<>(m);
            bad.put("helper", "other");
            reject(name, bad, t);
        }

        System.out.println("PASS inactive identity recipe dispatch and helper/pin negatives");
    }
}
