package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/**
 * Actual ordered manifest+DEX preparation boundaries, without APK packaging, helper production or
 * guest.
 */
public final class HiboardRecipeChainTest {
    static Map<String, Object> startupRecipe;
    static final String LIBS = "api30-hiboard-libraries.json",
            ROUTE = "api30-hiboard-routing.json",
            START = "api30-hiboard-startup.json";

    static final class Target implements Recipes.Target {
        final Path original;
        final byte[] baselineDex, baselineManifest;
        byte[] dex, manifest;
        String pin;
        Map<String, Object> constants = Map.of();
        boolean planValidation;
        Map<String, Object> startup;
        int api = 30, mutations;
        List<String> events = new ArrayList<>();

        Target(Path original) throws Exception {
            this.original = original;
            pin = FilesSupport.hash(original);
            startup = startupRecipe;

            try (var zip = new ZipFile(original.toFile())) {
                dex = zip.getInputStream(zip.getEntry("classes.dex")).readAllBytes();
                manifest = zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
            }

            baselineDex = dex;
            baselineManifest = manifest;
        }

        @Override
        public String profile() {
            return TestProfiles.SYNTHETIC;
        }

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
        public void addDex(String name) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path path, String name) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> names) throws Exception {
            manifest = ManifestLibraries.add(manifest, names);
            mutations++;
            events.add("libraries");
        }

        @Override
        public void guardHiboardStartup(boolean apply) throws Exception {
            if (apply) {
                dex = HiboardStartupGuard.apply(constants, api, original, dex);
                mutations++;
                events.add("startup");
            } else if (planValidation) {
                HiboardStartupGuard.validateOriginalChain(constants, api, original, dex);
                events.add("plan-startup");
            } else {
                HiboardStartupGuard.validateRouted(constants, api, original, dex);
                events.add("validate-startup");
            }
        }

        @Override
        public void routeHiboard(boolean apply) throws Exception {
            if (apply) {
                dex = HiboardRouting.apply(constants, api, original, dex);
                mutations++;
                events.add("route");
            } else {
                HiboardRouting.validate(constants, api, original, dex);
                events.add("validate-route");
            }
        }
    }

    static Map<String, Object> read(Path path) throws Exception {
        var value = Json.object(Json.parse(Files.readString(path)));

        value.put("sourceSha256", FilesSupport.hash(path));

        return value;
    }

    static Map<String, Object> copy(Map<String, Object> value) {
        return Json.object(Json.parse(Json.write(value)));
    }

    static void chain(
            Target target, String first, Map<String, Object> libraries, Map<String, Object> routing)
            throws Exception {
        Failure.require(
                target.api() == 30, "PATCH_API_UNSUPPORTED", "API30 Hiboard chain required", 5);
        target.planValidation = true;
        step(target, first, libraries, false);
        step(target, ROUTE, routing, false);
        step(target, START, target.startup, false);
        target.planValidation = false;
        step(target, first, libraries, true);
        step(target, ROUTE, routing, true);
        step(target, START, target.startup, true);
    }

    // Runs one step with its constants, as PatchTarget does.
    static void step(Target target, String name, Map<String, Object> recipe, boolean apply)
            throws Exception {
        target.constants = Json.obj(recipe, "constants");

        if (apply) {
            Recipes.apply(name, recipe, target);
        } else {
            Recipes.validateApi30(name, recipe, target);
        }
    }

    static void reject(Target target, Map<String, Object> libraries, Map<String, Object> routing)
            throws Exception {
        boolean failed = false;

        try {
            chain(target, LIBS, libraries, routing);
        } catch (Failure expected) {
            failed = true;
        }

        if (!failed
                || target.mutations != 0
                || !Arrays.equals(target.dex, target.baselineDex)
                || !Arrays.equals(target.manifest, target.baselineManifest)) {
            throw new AssertionError("Entire ordered chain must preflight before any mutation");
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 3 || args.length > 4) {
            throw new IllegalArgumentException(
                    "ROOT DRAFT_PROFILE_JSON ORIGINAL_APK [--common|STARTUP_RECIPE_JSON]");
        }

        Path root = Path.of(args[0]).toRealPath(), original = Path.of(args[2]).toRealPath();
        Path patches =
                root.resolve(
                        "profiles/"
                                + TestProfiles.pinning(root, FilesSupport.hash(original))
                                + "/patches");
        var definition = Json.object(Json.parse(Files.readString(Path.of(args[1]))));

        if (!Json.list(Json.obj(Json.obj(definition, "patches"), "Hiboard").get("steps"))
                .equals(List.of(LIBS, ROUTE, START))) {
            throw new AssertionError("Full draft must bind the exact ordered Hiboard chain");
        }

        String first =
                args.length == 4 && args[3].equals("--common") ? "framework-libraries.json" : LIBS;
        var libraries = read(patches.resolve(first));
        var routing = read(patches.resolve(ROUTE));
        startupRecipe =
                read(
                        args.length == 4 && !args[3].equals("--common")
                                ? Path.of(args[3])
                                : patches.resolve(START));
        var target = new Target(original);
        chain(target, first, libraries, routing);

        if (target.mutations != 3
                || !target.events.equals(
                        List.of(
                                "validate-route",
                                "plan-startup",
                                "libraries",
                                "validate-route",
                                "route",
                                "validate-startup",
                                "startup"))
                || Arrays.equals(target.manifest, target.baselineManifest)
                || !NativeCore.hash(target.dex)
                        .equals(
                                "e57744c0fa72be08541d469a37d5373283648133359e2c3faaa52f4a387db6d6")) {
            throw new AssertionError(
                    "Actual three-library manifest dependency must precede exact original DEX"
                            + " routing after complete preflight");
        }

        var wrongApi = new Target(original);
        wrongApi.api = 28;
        reject(wrongApi, libraries, routing);
        var wrongPin = new Target(original);
        wrongPin.pin = "0".repeat(64);
        reject(wrongPin, libraries, routing);
        var wrongLibraries = copy(libraries);
        wrongLibraries.put(
                "calls",
                List.of(Map.of("operation", "useLibraries", "args", List.of(List.of("QGAPI")))));
        reject(new Target(original), wrongLibraries, routing);
        var wrongOperation = copy(routing);
        wrongOperation.put("calls", List.of(Map.of("operation", "unknown", "args", List.of())));
        reject(new Target(original), libraries, wrongOperation);
        var extraLibraries = copy(libraries);
        extraLibraries.put(
                "calls",
                List.of(
                        Map.of(
                                "operation",
                                "useLibraries",
                                "args",
                                List.of(List.of("QGAPI", "QGMOD", "QGVehicle", "unexpected")))));
        reject(new Target(original), extraLibraries, routing);

        if (!FilesSupport.hash(original).equals(target.pin)) {
            throw new AssertionError("OEM original mutated");
        }

        System.out.println(
                "Actual ordered Hiboard library-manifest then guarded DEX chain PASS; five"
                        + " API/pin/libs/op negatives reject before any mutation; no"
                        + " APK/guest produced");
    }
}
