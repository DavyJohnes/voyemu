package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class PassionMusicDayDrawableRecipeTest {
    static final class Target implements Recipes.Target {
        int mutations, validations;
        String pin = PassionMusicDayDrawableGuard.PIN;

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
        public void addDex(String x) {
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
            mutations++;
        }

        @Override
        public void guardPassionMusicDayDrawables(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static Object call(String op, Object... args) {
        return Map.of("operation", op, "args", List.of(args));
    }

    static Map<String, Object> recipe(List<Object> calls) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", List.of(PassionMusicDayDrawableGuard.PIN)),
                "calls",
                calls);
    }

    public static void main(String[] argv) throws Exception {
        Object guard = call("guardPassionMusicDayDrawables");
        String name = PassionMusicDayDrawableGuard.NAME + ".json";
        var good = new Target();

        Recipes.apply(name, recipe(List.of(guard)), good);

        if (good.validations != 1 || good.mutations != 1) {
            throw new AssertionError();
        }

        for (var calls :
                List.of(
                        List.of(guard, guard),
                        List.of(guard, call("unknown")),
                        List.of(call("guardPassionMusicDayDrawables", "extra")),
                        List.of(call("useLibraries", List.of("android.car")), guard))) {
            var t = new Target();
            boolean denied = false;

            try {
                Recipes.apply(name, recipe(calls), t);
            } catch (Failure e) {
                denied = true;
            }

            if (!denied || t.mutations != 0) {
                throw new AssertionError();
            }
        }

        var t = new Target();
        boolean denied = false;

        try {
            Recipes.apply("foreign.json", recipe(List.of(guard)), t);
        } catch (Failure e) {
            denied = true;
        }

        if (!denied || t.mutations != 0) {
            throw new AssertionError();
        }

        t = new Target();
        t.pin = "0".repeat(64);
        denied = false;

        try {
            Recipes.apply(name, recipe(List.of(guard)), t);
        } catch (Failure e) {
            denied = true;
        }

        if (!denied || t.mutations != 0) {
            throw new AssertionError();
        }

        System.out.println(
                "Exact resource-only recipe GREEN; six malformed/foreign/no-mutation negatives");
    }
}
