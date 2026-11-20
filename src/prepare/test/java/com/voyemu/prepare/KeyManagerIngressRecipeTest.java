package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/** Full-list prevalidation and actual pinned two-step DEX pipeline; no APK signing/install. */
public final class KeyManagerIngressRecipeTest {
    static final String PIN = "951abb4f86540663fd654138b7e58cd0226d5dfa0470a4f5e85384bcdedbb78e";
    static final Map<String, Object> STEP = Map.of(StepConstants.PINS, List.of(PIN));

    static class Target implements Recipes.Target {
        int api = 30, mutations, validations;
        String pin = PIN;
        Path original;
        byte[] dex;

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
        }

        @Override
        public void guardKeyManager(boolean apply) throws Exception {
            if (apply) {
                mutations++;

                if (original != null) {
                    dex = KeyManagerGuard.apply(STEP, api, original, dex);
                }
            } else {
                validations++;

                if (original != null) {
                    KeyManagerGuard.validate(STEP, api, original, dex);
                }
            }
        }

        @Override
        public void guardKeyIngress(boolean apply) throws Exception {
            if (apply) {
                mutations++;

                if (original != null) {
                    dex = KeyManagerIngressGuard.apply(STEP, api, original, dex);
                }
            } else {
                validations++;

                if (original != null) {
                    KeyManagerIngressGuard.validateOriginal(STEP, api, original);
                }
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

    static void denied(String name, Map<String, Object> recipe, Target target) throws Exception {
        try {
            Recipes.apply(name, recipe, target);
        } catch (Failure expected) {
            if (target.mutations != 0) {
                throw new AssertionError("partial mutation");
            }

            return;
        }

        throw new AssertionError("invalid recipe accepted");
    }

    public static void main(String[] args) throws Exception {
        String name = KeyManagerIngressGuard.NAME + ".json";
        Object ingress = call("guardKeyIngress");
        var metadata = recipe(List.of(ingress));
        var target = new Target();

        Recipes.validateApi30(name, metadata, target);

        if (target.validations != 1 || target.mutations != 0) {
            throw new AssertionError();
        }

        Recipes.apply(name, metadata, target);

        if (target.validations != 2 || target.mutations != 1) {
            throw new AssertionError();
        }

        for (var calls :
                List.of(
                        List.of(ingress, ingress),
                        List.of(call("guardKeyIngress", "extra")),
                        List.of(ingress, call("unknown")),
                        List.of(call("useLibraries", List.of("QGAPI")), ingress),
                        List.of(call("guardKeyManager")),
                        List.of(call("unknown")))) {
            denied(name, recipe(calls), new Target());
        }

        denied("other.json", metadata, new Target());
        target = new Target();
        target.pin = "0".repeat(64);
        denied(name, metadata, target);
        target = new Target();
        target.api = 28;
        denied(name, metadata, target);

        if (args.length == 2) {
            Path profile = Path.of(args[0]);
            target = new Target();
            target.original = Path.of(args[1]);
            target.dex = KeyManagerIngressGuard.validateOriginal(STEP, 30, target.original);
            var steps = List.of("api30-keymanager.json", name);
            var recipes = new ArrayList<Map<String, Object>>();

            for (String step : steps) {
                var item =
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                profile.resolve("patches").resolve(step))));
                item.put(
                        "sourceSha256",
                        FilesSupport.hash(profile.resolve("patches").resolve(step)));
                recipes.add(item);
            }

            byte[] before = target.dex.clone();

            for (int i = 0; i < 2; i++) {
                Recipes.validateApi30(steps.get(i), recipes.get(i), target);
            }

            if (!Arrays.equals(before, target.dex)) {
                throw new AssertionError("prevalidation wrote DEX");
            }

            for (int i = 0; i < 2; i++) {
                Recipes.apply(steps.get(i), recipes.get(i), target);
            }

            if (target.mutations != 2 || Arrays.equals(before, target.dex)) {
                throw new AssertionError("ordered pipeline missing");
            }

            KeyManagerIngressGuard.validateOriginal(
                    STEP, 30, target.original); // validation remains original-based after mutation
        }

        System.out.println(
                "Exact recipe/full-list validation/no partial mutation + nine negatives; actual"
                        + " pinned two-step pipeline PASS (no APK install/runtime)");
    }
}
