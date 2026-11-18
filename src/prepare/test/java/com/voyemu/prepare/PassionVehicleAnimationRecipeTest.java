package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class PassionVehicleAnimationRecipeTest {
    static final class Target implements Recipes.Target {
        int mutations, validations;
        String pin = PassionVehicleAnimationGuard.PIN;

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
        public void guardPassionVehicleAnimation(boolean apply) {
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
                Map.of("original_sha256_allowlist", List.of(PassionVehicleAnimationGuard.PIN)),
                "calls",
                calls);
    }

    public static void main(String[] args) throws Exception {
        Object guard = call("guardPassionVehicleAnimation");
        String name = PassionVehicleAnimationGuard.NAME + ".json";
        Target good = new Target();

        Recipes.apply(name, recipe(List.of(guard)), good);

        if (good.validations != 1 || good.mutations != 1) {
            throw new AssertionError();
        }

        for (var calls :
                List.<List<Object>>of(
                        List.of(),
                        List.of(call("useLibraries", List.of("android.car"))),
                        List.of(call("unknown")),
                        List.of(guard, guard),
                        List.of(guard, call("unknown")),
                        List.of(call("guardPassionVehicleAnimation", "extra")),
                        List.of(call("useLibraries", List.of("android.car")), guard))) {
            Target t = new Target();
            boolean denied = false;

            try {
                Recipes.apply(name, recipe(calls), t);
            } catch (Failure expected) {
                denied = true;
            }

            if (!denied || t.mutations != 0) {
                throw new AssertionError();
            }
        }

        Target t = new Target();
        boolean denied = false;

        try {
            Recipes.apply("foreign.json", recipe(List.of(guard)), t);
        } catch (Failure expected) {
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
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || t.mutations != 0) {
            throw new AssertionError();
        }

        System.out.println(
                "Exact animation recipe; malformed and foreign inputs rejected before mutation");
    }
}
