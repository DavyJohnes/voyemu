package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Finite recipe dispatch and no mutation on malformed calls. */
public final class PassionUsbMediaSessionRecipeTest {
    static final class Target implements Recipes.Target {
        int mutations, validations, api = 30;
        String pin = PassionUsbMediaSessionGuard.APK;

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
        public void addDex(String value) {
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
        public void useLibraries(List<String> names) {
            mutations++;
        }

        @Override
        public void guardPassionUsbMediaSession(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static void check(boolean ok) {
        if (!ok) {
            throw new AssertionError("Exact USB media recipe dispatch");
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
                Map.of("original_sha256_allowlist", List.of(PassionUsbMediaSessionGuard.APK)),
                "calls",
                calls);
    }

    static void reject(String name, Map<String, Object> recipe, Target target) throws Exception {
        boolean denied = false;

        try {
            Recipes.apply(name, recipe, target);
        } catch (Failure failure) {
            denied = true;
        }

        check(denied && target.mutations == 0);
    }

    public static void main(String[] args) throws Exception {
        String name = PassionUsbMediaSessionGuard.NAME + ".json";
        Object guard = call("guardPassionUsbMediaSession");
        var good = new Target();

        Recipes.apply(name, recipe(List.of(guard)), good);
        check(good.validations == 1 && good.mutations == 1);

        for (var calls :
                List.of(
                        List.of(guard, guard),
                        List.of(guard, call("unknown")),
                        List.of(call("guardPassionUsbMediaSession", "extra")),
                        List.of(call("useLibraries", List.of("android.car")), guard))) {
            reject(name, recipe(calls), new Target());
        }

        reject(name, recipe(List.of()), new Target());
        reject(name, recipe(List.of(call("useLibraries", List.of("android.car")))), new Target());
        reject("foreign.json", recipe(List.of(guard)), new Target());
        var wrongPin = new Target();
        wrongPin.pin = "0".repeat(64);
        reject(name, recipe(List.of(guard)), wrongPin);
        var wrongApi = new Target();
        wrongApi.api = 29;
        reject(name, recipe(List.of(guard)), wrongApi);
        var foreignPins = new TreeMap<String, Object>(recipe(List.of(guard)));
        foreignPins.put(
                "constants",
                Map.of(
                        "original_sha256_allowlist",
                        List.of(PassionUsbMediaSessionGuard.APK, "0".repeat(64))));
        reject(name, foreignPins, new Target());

        if (args.length > 0) {
            Path profile =
                    Path.of(args[0])
                            .resolve(
                                    "profiles/"
                                            + TestProfiles.declaring(
                                                    Path.of(args[0]),
                                                    PassionUsbMediaSessionGuard.NAME));
            var real =
                    Json.object(Json.parse(Files.readString(profile.resolve("patches/" + name))));
            real.put("sourceSha256", "a".repeat(64));
            var target = new Target();
            Recipes.apply(name, real, target);
            check(target.validations == 1 && target.mutations == 1);
            var definition =
                    Json.obj(
                            Json.object(
                                    Json.parse(Files.readString(profile.resolve("profile.json")))),
                            "patches");
            var steps = Json.list(Json.obj(definition, "LY_Multimedia").get("steps"));
            check(
                    steps.equals(
                            List.of(
                                    "passion-api30-bluetooth-role.json",
                                    "passion-api30-music-foreground.json",
                                    "passion-api30-music-day-drawables.json",
                                    "passion-api30-music-insets.json",
                                    name)));
        }

        System.out.println(
                "USB media finite recipe/profile chain PASS: exact dispatch and ten no-mutation"
                        + " negatives");
    }
}
