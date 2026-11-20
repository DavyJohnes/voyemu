package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class HiboardRecipeTest {
    static final String PIN = "1".repeat(64);

    static final class Target implements Recipes.Target {
        int mutations, validations;
        String pin = PIN;

        @Override
        public String profile() {
            return TestProfiles.SYNTHETIC;
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
            if (!names.equals(List.of("QGAPI", "QGMOD", "QGVehicle"))) {
                throw new AssertionError("Exact original Hiboard framework dependencies required");
            }

            mutations++;
        }

        @Override
        public void routeHiboard(boolean apply) {
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

    static Map<String, Object> recipe(List<Object> calls, List<Object> pins) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", pins),
                "calls",
                calls,
                "helper",
                "api30-hiboard-routing/src/com/voyemu/output/HiboardRouter.java");
    }

    static void reject(String name, List<Object> calls, List<Object> pins, String original)
            throws Exception {
        var target = new Target();

        target.pin = original;
        boolean denied = false;

        try {
            Recipes.apply(name, recipe(calls, pins), target);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || target.mutations != 0) {
            throw new AssertionError("Complete Hiboard metadata must reject before mutation");
        }
    }

    public static void main(String[] args) throws Exception {
        Object route = call("routeHiboard");
        String name = HiboardRouting.NAME + ".json";
        List<Object> pins = List.of(PIN);
        var target = new Target();

        Recipes.validateApi30(name, recipe(List.of(route), pins), target);

        if (target.mutations != 0 || target.validations != 1) {
            throw new AssertionError();
        }

        Recipes.apply(name, recipe(List.of(route), pins), target);

        if (target.mutations != 1 || target.validations != 2) {
            throw new AssertionError();
        }

        for (var calls :
                List.of(
                        List.of(route, route),
                        List.of(route, call("unknown")),
                        List.of(call("routeHiboard", "extra")),
                        List.of(call("useLibraries", List.of("QGAPI")), route),
                        List.of(route, call("useLibraries", List.of("QGAPI"))))) {
            reject(name, calls, pins, PIN);
        }

        reject("other.json", List.of(route), pins, PIN);
        reject(name, List.of(route), pins, "0".repeat(64));

        if (args.length >= 1) {
            var metadata = Json.object(Json.parse(Files.readString(Path.of(args[0]))));
            metadata.put("sourceSha256", FilesSupport.hash(Path.of(args[0])));
            var actual = new Target();
            actual.pin = StepConstants.pins(Json.obj(metadata, "constants")).get(0);
            Recipes.apply(name, metadata, actual);

            if (actual.mutations != 1 || actual.validations != 1) {
                throw new AssertionError(
                        "Canonical recipe must execute the same prevalidated operation");
            }
        }

        if (args.length == 2) {
            Path path = Path.of(args[1]);
            var metadata = Json.object(Json.parse(Files.readString(path)));
            metadata.put("sourceSha256", FilesSupport.hash(path));
            var libraries = new Target();
            libraries.pin = StepConstants.pins(Json.obj(metadata, "constants")).get(0);
            Recipes.validateApi30(path.getFileName().toString(), metadata, libraries);

            if (libraries.mutations != 0) {
                throw new AssertionError("Hiboard libraries must fully prevalidate");
            }

            Recipes.apply(path.getFileName().toString(), metadata, libraries);

            if (libraries.mutations != 1 || libraries.validations != 0) {
                throw new AssertionError(
                        "Exact Hiboard original must accept original three framework dependencies"
                                + " before routing");
            }
        }

        System.out.println(
                "Exact Hiboard recipe, prevalidation, one mutation, and seven metadata negatives"
                        + " PASS");
    }
}
