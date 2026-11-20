package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class PassionPhoneRecipeTest {
    static final class Target implements Recipes.Target {
        int mutations, validations, api = 30;
        String pin = PassionPhoneGuard.APK;

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
        public void guardPassionPhone(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static void need(boolean value) {
        if (!value) {
            throw new AssertionError("Exact synthetic phone recipe dispatch");
        }
    }

    static Object call(String name, Object... args) {
        return Map.of("operation", name, "args", List.of(args));
    }

    static Map<String, Object> recipe(List<Object> calls) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", List.of(PassionPhoneGuard.APK)),
                "calls",
                calls);
    }

    static void reject(String name, Map<String, Object> recipe, Target target) throws Exception {
        try {
            Recipes.apply(name, recipe, target);
            throw new AssertionError("foreign recipe accepted");
        } catch (Failure expected) {
            need(target.mutations == 0);
        }
    }

    public static void main(String[] args) throws Exception {
        String name = PassionPhoneGuard.NAME + ".json";
        Object guard = call("guardPassionPhone");
        var good = new Target();

        Recipes.apply(name, recipe(List.of(guard)), good);
        need(good.validations == 1 && good.mutations == 1);

        for (var calls :
                List.of(
                        List.of(guard, guard),
                        List.of(guard, call("unknown")),
                        List.of(call("guardPassionPhone", "extra")),
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
                        List.of(PassionPhoneGuard.APK, "0".repeat(64))));
        reject(name, foreignPins, new Target());

        if (args.length > 0) {
            Path profile =
                    Path.of(args[0])
                            .resolve(
                                    "profiles/"
                                            + TestProfiles.declaring(
                                                    Path.of(args[0]), PassionPhoneGuard.NAME));
            PassionPhoneGuard.validateHelperSource(
                    profile.resolve("patches/" + PassionPhoneGuard.HELPER_ROOT));
            var real =
                    Json.object(Json.parse(Files.readString(profile.resolve("patches/" + name))));
            real.put("sourceSha256", "a".repeat(64));
            var target = new Target();
            Recipes.apply(name, real, target);
            need(target.validations == 1 && target.mutations == 1);
            var definition =
                    Json.object(Json.parse(Files.readString(profile.resolve("profile.json"))));
            var steps =
                    Json.list(Json.obj(Json.obj(definition, "patches"), "LY_BtPhone").get("steps"));
            need(steps.equals(List.of("passion-api30-bluetooth-role.json", name)));
            need("components/phone".equals(Json.obj(definition, "components").get("phone")));
            var grants = Json.obj(Json.obj(definition, "compatibility"), "privapp_whitelist");
            need(
                    List.of("android.permission.MODIFY_PHONE_STATE")
                            .equals(Json.obj(grants, "packages").get("com.voyemu.passion.phone")));
            need("files/voyemu-framework.xml".equals(Json.str(grants, "file")));
        }

        System.out.println(
                "PASS Passion synthetic phone finite recipe/profile chain, approved exact"
                        + " privileged grant, exact dispatch and ten no-mutation negatives");
    }
}
