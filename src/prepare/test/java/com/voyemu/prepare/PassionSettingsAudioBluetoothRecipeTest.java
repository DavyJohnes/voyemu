package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** Exact recipe dispatch, complete source tree and production helper DEX. */
public final class PassionSettingsAudioBluetoothRecipeTest {
    static final class Target implements Recipes.Target {
        int validations, mutations;
        String pin = PassionSettingsSoundGuard.APK;
        int api = 30;

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
        public void addDex(String s) {
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
        public void guardPassionSettingsSound(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }

        @Override
        public void guardPassionSettingsBluetoothRole(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static void check(boolean ok) {
        if (!ok) {
            throw new AssertionError("Passion Settings recipe contract");
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
                Map.of("original_sha256_allowlist", List.of(PassionSettingsSoundGuard.APK)),
                "calls",
                calls);
    }

    static void reject(String name, Map<String, Object> value, Target target) throws Exception {
        boolean denied = false;

        try {
            Recipes.apply(name, value, target);
        } catch (Failure e) {
            denied = true;
        }

        check(denied && target.mutations == 0);
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]),
                work = Path.of(args[1]),
                apk = Path.of(args[2]),
                platform = Path.of(args[3]);

        for (String[] row :
                new String[][] {
                    {PassionSettingsSoundGuard.NAME, "guardPassionSettingsSound"},
                    {PassionSettingsBluetoothRoleGuard.NAME, "guardPassionSettingsBluetoothRole"}
                }) {
            String name = row[0] + ".json";
            Object op = call(row[1]);
            Target valid = new Target();
            Recipes.apply(name, recipe(List.of(op)), valid);
            check(valid.validations == 1 && valid.mutations == 1);
            reject(name, recipe(List.of(op, op)), new Target());
            reject(name, recipe(List.of(call(row[1], "extra"))), new Target());
            reject("foreign.json", recipe(List.of(op)), new Target());
            Target wrong = new Target();
            wrong.api = 29;
            reject(name, recipe(List.of(op)), wrong);
            wrong = new Target();
            wrong.pin = "0".repeat(64);
            reject(name, recipe(List.of(op)), wrong);
            Path file =
                    root.resolve(
                            "profiles/" + PassionSettingsSoundGuard.PROFILE + "/patches/" + name);
            var real = Json.object(Json.parse(Files.readString(file)));
            real.put("sourceSha256", "a".repeat(64));
            Target current = new Target();
            Recipes.apply(name, real, current);
            check(current.validations == 1 && current.mutations == 1);
        }

        Path source =
                root.resolve(
                        "profiles/"
                                + PassionSettingsSoundGuard.PROFILE
                                + "/patches/"
                                + PassionSettingsSoundGuard.NAME
                                + "/src");
        PassionSettingsSoundGuard.validateSource(source);
        Path dex =
                PassionSettingsSoundGuard.compileHelper(
                        PassionSettingsSoundGuard.PROFILE,
                        30,
                        apk,
                        source,
                        platform,
                        work.resolve("helper"));
        check(Files.isRegularFile(dex));
        System.out.println(
                "Passion Settings exact recipe dispatch, negative cases and pinned helper DEX"
                        + " PASS");
    }
}
