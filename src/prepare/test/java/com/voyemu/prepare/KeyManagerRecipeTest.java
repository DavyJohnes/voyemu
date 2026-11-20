package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class KeyManagerRecipeTest {
    static final String PIN = "951abb4f86540663fd654138b7e58cd0226d5dfa0470a4f5e85384bcdedbb78e";

    static final class Target implements Recipes.Target {
        int mutations, validations;
        String pin = PIN;

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
            if (!names.equals(List.of("QGAPI", "QGMOD", "QGVehicle", "VoyemuFreeWheel"))) {
                throw new AssertionError();
            }

            mutations++;
        }

        @Override
        public void guardKeyManager(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static Object call(String operation, Object... args) {
        return Map.of("operation", operation, "args", List.of(args));
    }

    static Map<String, Object> recipe(List<Object> calls) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", List.of(PIN)),
                "calls",
                calls);
    }

    static void reject(String name, List<Object> calls, String pin) throws Exception {
        var target = new Target();

        target.pin = pin;
        boolean denied = false;

        try {
            Recipes.apply(name, recipe(calls), target);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || target.mutations != 0) {
            throw new AssertionError("Full prevalidation must refuse before mutation");
        }
    }

    public static void main(String[] args) throws Exception {
        Object guard = call("guardKeyManager"),
                libs =
                        call(
                                "useLibraries",
                                List.of("QGAPI", "QGMOD", "QGVehicle", "VoyemuFreeWheel"));
        String name = KeyManagerGuard.NAME + ".json";
        var target = new Target();

        Recipes.validateApi30(name, recipe(List.of(guard, libs)), target);

        if (target.mutations != 0 || target.validations != 1) {
            throw new AssertionError();
        }

        Recipes.apply(name, recipe(List.of(guard, libs)), target);

        if (target.mutations != 2 || target.validations != 2) {
            throw new AssertionError();
        }

        for (var calls :
                List.of(
                        List.of(libs, guard),
                        List.of(guard, libs, guard),
                        List.of(guard),
                        List.of(call("guardKeyManager", "extra"), libs),
                        List.of(guard, libs, call("unknown")))) {
            reject(name, calls, PIN);
        }

        reject("other.json", List.of(guard, libs), PIN);
        reject(name, List.of(guard, libs), "0".repeat(64));
        System.out.println(
                "Exact OEM KeyManager recipe/libraries/order/prevalidation and eight negatives"
                        + " PASS");
    }
}
