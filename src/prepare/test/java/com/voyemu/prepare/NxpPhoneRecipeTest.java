package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class NxpPhoneRecipeTest {
    static final String PIN = "55e7602d15f5d677a5ddeaaa44c0c04c355235c5322b51edefa3a6ef29636451";

    static final class Target implements Recipes.Target {
        String id = TestProfiles.SYNTHETIC, pin = PIN;
        int api = 28, calls;

        @Override
        public String profile() {
            return id;
        }

        @Override
        public String originalSha256() {
            return pin;
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
            throw new AssertionError();
        }

        @Override
        public Path output() {
            throw new AssertionError();
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String x) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String x) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String x) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> x) {
            throw new AssertionError();
        }

        @Override
        public void addNxpPhoneProvider() {
            calls++;
        }
    }

    static String source;

    static Map<String, Object> metadata() {
        return Map.of("sourceSha256", source, "constants", Map.of(), "calls", List.of());
    }

    static void reject(Target target) throws Exception {
        int before = target.calls;

        try {
            Recipes.apply("nxp-phone-provider.json", metadata(), target);
            throw new AssertionError("guard bypass");
        } catch (Failure expected) {
            if (before != target.calls) {
                throw new AssertionError("mutation before guard");
            }
        }
    }

    public static void main(String[] args) throws Exception {
        source =
                NativeCore.hash(
                        java.nio.file.Files.readAllBytes(
                                java.nio.file.Path.of(args[0])
                                        .resolve(
                                                "profiles/"
                                                        + TestProfiles.pinning(
                                                                java.nio.file.Path.of(args[0]), PIN)
                                                        + "/patches/nxp-phone-provider.json")));
        Target target = new Target();
        Recipes.apply("nxp-phone-provider.json", metadata(), target);

        if (target.calls != 1) {
            throw new AssertionError();
        }

        target.id = TestProfiles.SYNTHETIC;
        target.api = 30;
        reject(target);
        target.api = 28;
        target.pin = "d5a7417be79e2e4bfd70e643df85ae76e1a8a15332a345f126ccc5781f5ba8a3";
        reject(target);
        System.out.println(
                "NXP Phone recipe PASS exact profile, API and original guards before mutation");
    }
}
