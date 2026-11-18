package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/** Applies the NXP RC45 policy node to the pinned Google policy and the imported OEM policies. */
public final class NxpBoundedPolicyTest {
    static final String PROFILE = "free-09-chinese-20221230-nxp-rc45";

    static final class Target implements Recipes.Target {
        final Path file;
        final Map<String, Path> uses;

        Target(Path file, Map<String, Path> uses) {
            this.file = file;
            this.uses = uses;
        }

        @Override
        public String profile() {
            return PROFILE;
        }

        @Override
        public int api() {
            return 28;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return file.getParent();
        }

        @Override
        public Path output() {
            return file;
        }

        @Override
        public Map<String, Path> uses() {
            return uses;
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
        public void useLibraries(List<String> s) {
            throw new AssertionError();
        }
    }

    static Map<String, Object> recipe(String name) throws Exception {
        return Json.object(
                Json.parse(
                        Files.readString(
                                Path.of(System.getProperty("user.dir"))
                                        .resolve("profiles/" + PROFILE + "/patches/" + name))));
    }

    /** Arguments: Google plat_sepolicy.cil, its policy hash file, OEM plat and vendor policies. */
    public static void main(String[] a) throws Exception {
        Path root = Files.createTempDirectory("nxp-policy-test-"),
                policy = root.resolve("policy"),
                hash = root.resolve("hash");

        Files.copy(Path.of(a[0]), policy);
        Files.copy(Path.of(a[1]), hash);
        var donors =
                Map.of(
                        "original:PlatSepolicy",
                        Path.of(a[2]),
                        "original:VendorSepolicy",
                        Path.of(a[3]));
        byte[] clean = Files.readAllBytes(policy);

        try {
            Recipes.apply(
                    "selinux-canbus-nxp.json",
                    recipe("selinux-canbus-nxp.json"),
                    new Target(policy, Map.of()));
            throw new AssertionError("NXP canbus step without the OEM policies accepted");
        } catch (Failure expected) {
            if (!Arrays.equals(clean, Files.readAllBytes(policy))) {
                throw new AssertionError("rejected NXP canbus step changed the policy");
            }
        }

        Target node = new Target(policy, donors);
        Recipes.apply("selinux-canbus-nxp.json", recipe("selinux-canbus-nxp.json"), node);
        Recipes.apply("selinux-bounded-su-nxp.json", recipe("selinux-bounded-su-nxp.json"), node);
        String bounded = Files.readString(policy);

        if (bounded.contains("(typepermissive su)")
                || !bounded.contains("\n; voyemu NXP bounded su\n")
                || !bounded.contains("(allow su self (process (execmem)))\n")
                || bounded.contains("_28_0 ")
                || !bounded.endsWith("(allow su traced_enabled_prop (property_service (set)))\n")) {
            throw new AssertionError("NXP su is not bounded by the OEM rules and carve-outs");
        }

        Recipes.apply(
                "selinux-hash-nxp.json",
                recipe("selinux-hash-nxp.json"),
                new Target(hash, Map.of("patched:selinux-canbus-nxp", policy)));

        if (!Files.readString(hash, StandardCharsets.UTF_8)
                .equals(NativeCore.hash(bounded.getBytes(StandardCharsets.UTF_8)) + "\n")) {
            throw new AssertionError("NXP policy hash differs from the bounded policy");
        }
    }
}
