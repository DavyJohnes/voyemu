package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class SettingsVoiceCloneRecipeTest {
    static final String PIN = "6f96bbeb58bf915b0a2d60817082cdca3523e3a2d00c144544cb92b4e5be6870";

    static final class Target implements Recipes.Target {
        int validations, mutations;
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
        public void useLibraries(List<String> s) {
            throw new AssertionError();
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
        public void guardSettingsVoiceClone(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static Map<String, Object> metadata(List<Object> calls, List<Object> pins) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", pins),
                "calls",
                calls);
    }

    static Object call(String name, Object... args) {
        return Map.of("operation", name, "args", List.of(args));
    }

    public static void main(String[] a) throws Exception {
        String name = SettingsVoiceCloneGuard.NAME + ".json";
        Object op = call("guardSettingsVoiceClone");
        List<Object> pins = List.of(PIN);
        var t = new Target();

        Recipes.validateApi30(name, metadata(List.of(op), pins), t);

        if (t.mutations != 0 || t.validations != 1) {
            throw new AssertionError();
        }

        Recipes.apply(name, metadata(List.of(op), pins), t);

        if (t.mutations != 1 || t.validations != 2) {
            throw new AssertionError();
        }

        int n = malformedCallRejections(name, op, pins);
        n += inverseRejections(name, op, pins);
        System.out.println(
                "Exact recipe prevalidation + one mutation + " + n + " metadata negatives PASS");
    }

    static int malformedCallRejections(String name, Object op, List<Object> pins) throws Exception {
        int n = 0;

        for (var calls :
                List.of(
                        List.of(op, op),
                        List.of(op, call("unknown")),
                        List.of(call("guardSettingsVoiceClone", "extra")),
                        List.of(call("useLibraries", List.of("QGAPI")), op))) {
            var x = new Target();

            try {
                Recipes.apply(name, metadata(calls, pins), x);
                throw new AssertionError("accepted metadata");
            } catch (Failure e) {
                if (x.mutations != 0) {
                    throw new AssertionError("mutated before validation");
                }

                n++;
            }
        }

        return n;
    }

    static int inverseRejections(String name, Object op, List<Object> pins) throws Exception {
        int n = 0;

        for (int i : new int[] {0, 2}) {
            var x = new Target();

            if (i == 2) {
                x.pin = "0".repeat(64);
            }

            try {
                Recipes.apply(i == 0 ? "other.json" : name, metadata(List.of(op), pins), x);
                throw new AssertionError("accepted inverse");
            } catch (Failure e) {
                if (x.mutations != 0) {
                    throw new AssertionError();
                }

                n++;
            }
        }

        return n;
    }
}
