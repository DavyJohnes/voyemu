package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class SettingsVoiceMuteRecipeTest {
    static final class Target implements Recipes.Target {
        int checks, mutations;
        String pin = SettingsVoiceMuteGuard.APK;

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
            throw new AssertionError();
        }

        @Override
        public void guardSettingsVoiceMute(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                checks++;
            }
        }
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
                calls);
    }

    static void reject(String name, Map<String, Object> data, Target target) throws Exception {
        try {
            Recipes.apply(name, data, target);
        } catch (Failure expected) {
            if (target.mutations != 0) {
                throw new AssertionError("mutation before rejection");
            }

            return;
        }

        throw new AssertionError("wrong recipe accepted");
    }

    public static void main(String[] args) throws Exception {
        var call = Map.of("operation", "guardSettingsVoiceMute", "args", List.of());
        String name = SettingsVoiceMuteGuard.NAME + ".json";
        var pins = List.<Object>of(SettingsVoiceMuteGuard.APK);
        var good = recipe(List.of(call), pins);
        var target = new Target();

        Recipes.apply(name, good, target);

        if (target.checks != 1 || target.mutations != 1) {
            throw new AssertionError("plan/apply mismatch");
        }

        reject("other.json", good, new Target());
        reject(name, recipe(List.of(call, call), pins), new Target());
        reject(
                name,
                recipe(
                        List.of(
                                Map.of(
                                        "operation",
                                        "guardSettingsVoiceMute",
                                        "args",
                                        List.of("extra"))),
                        pins),
                new Target());
        reject(name, recipe(List.of(call), List.of("0".repeat(64))), new Target());
        var wrong = new Target();
        wrong.pin = "0".repeat(64);
        reject(name, good, wrong);
    }
}
