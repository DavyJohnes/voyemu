package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class PassionDiagnosticsRecipeTest {
    static final class Target implements Recipes.Target {
        int mutations, validations;
        String pin = PassionDiagnosticsGuard.PIN;

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
        public void useLibraries(List<String> names) {
            mutations++;
        }

        @Override
        public void guardPassionDiagnostics(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static Map<String, Object> recipe(List<Object> calls) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", List.of(PassionDiagnosticsGuard.PIN)),
                "calls",
                calls);
    }

    static Object call(String operation, Object... args) {
        return Map.of("operation", operation, "args", List.of(args));
    }

    public static void main(String[] args) throws Exception {
        Object guard = call("guardPassionDiagnostics"),
                libraries = call("useLibraries", List.of("android.car"));
        String name = PassionDiagnosticsGuard.NAME + ".json";
        var target = new Target();

        Recipes.apply(name, recipe(List.of(libraries, guard)), target);

        if (target.mutations != 2 || target.validations != 1) {
            throw new AssertionError();
        }

        for (var calls :
                List.of(
                        List.of(libraries, guard, guard),
                        List.of(libraries, guard, call("unknown")),
                        List.of(libraries, call("guardPassionDiagnostics", "extra")))) {
            var bad = new Target();
            boolean denied = false;

            try {
                Recipes.apply(name, recipe(calls), bad);
            } catch (Failure e) {
                denied = true;
            }

            if (!denied || bad.mutations != 0) {
                throw new AssertionError("Prevalidation must reject before any mutation");
            }
        }

        var wrongName = new Target();
        boolean denied = false;

        try {
            Recipes.apply("other-api30.json", recipe(List.of(guard)), wrongName);
        } catch (Failure e) {
            denied = true;
        }

        if (!denied || wrongName.mutations != 0) {
            throw new AssertionError();
        }

        var wrongPin = new Target();
        wrongPin.pin = "0".repeat(64);
        denied = false;

        try {
            Recipes.apply(name, recipe(List.of(guard)), wrongPin);
        } catch (Failure e) {
            denied = true;
        }

        if (!denied || wrongPin.mutations != 0) {
            throw new AssertionError();
        }

        System.out.println(
                "Finite Passion optional diagnostics recipe prevalidation PASS: exact name/pin and"
                        + " five no-mutation negatives");
    }
}
