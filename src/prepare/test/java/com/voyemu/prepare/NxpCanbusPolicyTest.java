package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/**
 * Exercises production dispatch with synthetic inputs; explicit extracted inputs remain supported.
 */
public final class NxpCanbusPolicyTest {
    static final Map<String, String[]> FIXTURE_PINS = new java.util.HashMap<>();

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static final class Target implements Recipes.Target {
        Path output;
        Map<String, Path> uses = Map.of();
        String profile = TestProfiles.SYNTHETIC;
        int api = 28;

        @Override
        public String profile() {
            return profile;
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
        public Path output() {
            return output;
        }

        @Override
        public Path work() {
            return output.getParent();
        }

        @Override
        public Map<String, Path> uses() {
            return uses;
        }

        @Override
        public void addDex(String source) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path source, String name) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> names) {
            throw new AssertionError();
        }
    }

    static Map<String, Object> metadata(Path root, String name) throws Exception {
        Path recipe =
                root.resolve(
                        "profiles/"
                                + TestProfiles.declaring(root, "selinux-canbus-nxp")
                                + "/patches/"
                                + name
                                + ".json");

        if (!Files.exists(recipe)) {
            return Map.of("constants", Map.of());
        }

        var data = Json.object(Json.parse(Files.readString(recipe)));
        data.put("sourceSha256", FilesSupport.hash(recipe));

        if (FIXTURE_PINS.containsKey(name)) {
            var constants = Json.obj(data, "constants");
            String[] pins = FIXTURE_PINS.get(name);
            constants.put("INPUT_SHA256", pins[0]);
            constants.put("OUTPUT_SHA256", pins[1]);
            data.put("constants", constants);
        }

        return data;
    }

    static void apply(Path root, String name, Target target) throws Exception {
        Recipes.apply(name + ".json", metadata(root, name), target);
    }

    static Path fixture(Path root, Path work) throws Exception {
        Path input = work.resolve("input/system/etc/selinux");

        Files.createDirectories(input);
        String policy =
                """
                (class service_manager (add find))
                (classorder (service_manager))
                (sid kernel)
                (sidorder (kernel))
                (role object_r)
                (type system_app)
                (type platform_app)
                (type priv_app)
                (typeattribute service_manager_type)
                """;
        String contexts = "activity u:object_r:activity_service:s0\n",
                identity = "synthetic-original-policy\n";
        String patchedPolicy =
                policy
                        + Json.str(
                                Json.obj(metadata(root, "selinux-canbus-nxp"), "constants"),
                                "APPEND");
        String patchedContexts =
                contexts
                        + Json.str(
                                Json.obj(
                                        metadata(root, "selinux-service-contexts-nxp"),
                                        "constants"),
                                "APPEND");
        String patchedIdentity =
                NativeCore.hash(patchedPolicy.getBytes(java.nio.charset.StandardCharsets.UTF_8))
                        + "\n";
        String[] names = {"selinux-canbus-nxp", "selinux-service-contexts-nxp", "selinux-hash-nxp"};
        String[] members = {
            "plat_sepolicy.cil", "plat_service_contexts", "plat_and_mapping_sepolicy.cil.sha256"
        };
        String[] original = {policy, contexts, identity},
                patched = {patchedPolicy, patchedContexts, patchedIdentity};

        for (int i = 0; i < names.length; i++) {
            Path file = input.resolve(members[i]);
            Files.writeString(file, original[i]);
            FIXTURE_PINS.put(
                    names[i],
                    new String[] {
                        FilesSupport.hash(file),
                        NativeCore.hash(
                                patched[i].getBytes(java.nio.charset.StandardCharsets.UTF_8))
                    });
        }

        return input;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[args.length - 1]);

        Files.createDirectories(work);
        Path input =
                args.length == 2
                        ? fixture(root, work)
                        : Path.of(args[1]).resolve("system/etc/selinux");
        var definition =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + TestProfiles.declaring(
                                                                root, "selinux-canbus-nxp")
                                                        + "/profile.json"))));
        var profile =
                new ProfileRegistry.Profile(
                        TestProfiles.declaring(root, "selinux-canbus-nxp"),
                        "source",
                        definition,
                        "voyemu/profiles/"
                                + TestProfiles.declaring(root, "selinux-canbus-nxp")
                                + "/",
                        root);
        checkProfileDefinition(definition, profile);
        Target target = new Target();
        target.output = work.resolve("policy");
        Files.copy(
                input.resolve("plat_sepolicy.cil"),
                target.output,
                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        checkCanbusPolicy(root, target);
        Path patchedPolicy = target.output;

        for (String name :
                List.of("selinux-canbus-nxp", "selinux-service-contexts-nxp", "selinux-hash-nxp")) {
            checkPolicyMember(root, input, work, target, name, patchedPolicy);
        }

        System.out.println(
                "PASS NXP production policy/context/hash dispatch and mutation guards ("
                        + (args.length == 2 ? "synthetic fixture" : "pinned extracted inputs")
                        + ")");
    }

    static void checkProfileDefinition(
            Map<String, Object> definition, ProfileRegistry.Profile profile) throws Exception {
        var config = Json.obj(definition, "configure");

        check(
                !config.containsKey("services")
                        && Json.list(config.get("start"))
                                .equals(
                                        List.of(
                                                "com.qinggan.canbus.service/.RegistrationService",
                                                Map.of(
                                                        "component",
                                                        "com.voyemu.oem/.RegistrationService",
                                                        "mode",
                                                        "service"),
                                                Map.of(
                                                        "component",
                                                        "com.qinggan.sttservice/com.qinggan.speech.vui.VuiService",
                                                        "mode",
                                                        "service"))),
                "Factory must start both actual Binder services before the real OEM Voice service"
                        + " using the production configure.start schema");
        check(
                Json.list(Json.obj(definition, "verify").get("services"))
                        .equals(List.of("qg.canbus", "qg.key_manager", "qg.system_policy")),
                "Factory Binder readiness must require CAN, key manager and the proven SystemPolicy"
                        + " service");
        var exported = ProfileRegistry.patchMetadata(profile);

        for (String name :
                List.of(
                        "selinux-canbus-nxp.json",
                        "selinux-service-contexts-nxp.json",
                        "selinux-hash-nxp.json",
                        "nxp-music-media-compat.json",
                        "nxp-launcher-display-routing.json")) {
            check(exported.containsKey(name), "Production profile must declare " + name);
            check(
                    Recipes.hasRegisteredCoverage(name, Json.object(exported.get(name))),
                    "Canonical exported recipe must match production handler pin: " + name);
        }
    }

    static void checkCanbusPolicy(Path root, Target target) throws Exception {
        byte[] original = Files.readAllBytes(target.output);
        boolean undeclared = false;

        try {
            apply(root, "selinux-canbus-nxp", target);
        } catch (Failure expected) {
            undeclared = true;
        }

        check(
                undeclared && java.util.Arrays.equals(original, Files.readAllBytes(target.output)),
                "Canbus policy requires the declared OEM policies of the bounded su step");
        target.uses = donors(target.output.getParent());
        apply(root, "selinux-canbus-nxp", target);
        String policy = Files.readString(target.output);
        check(
                policy.startsWith(new String(original, java.nio.charset.StandardCharsets.UTF_8)),
                "All original policy bytes preserved");
        check(
                policy.contains("(roletype object_r voyemu_canbus_service)")
                        && policy.contains(
                                "(allow system_app voyemu_canbus_service (service_manager (add"
                                        + " find)))"),
                "Valid typed canbus registration");
        check(
                policy.contains("(roletype object_r voyemu_nxp_key_manager_service)")
                        && policy.contains(
                                "(allow system_app voyemu_nxp_key_manager_service (service_manager"
                                        + " (add find)))"),
                "Valid typed key manager registration");
        check(
                policy.contains("(type voyemu_nxp_system_policy_service)")
                        && policy.contains(
                                "(allow system_app voyemu_nxp_system_policy_service"
                                        + " (service_manager (add find)))"),
                "Exact SystemPolicy service registration and lookup");
        check(
                !policy.contains("(allow system_app default_android_service (service_manager (add")
                        && !policy.contains("(allow platform_app voyemu_nxp_system_policy_service")
                        && !policy.contains("(allow priv_app voyemu_nxp_system_policy_service"),
                "No broad default registration or unproven client domains");
        check(
                !policy.contains("voyemu_oem_service") && !policy.contains("bounded su"),
                "No unrelated A9 services or bounded su");
    }

    // The canbus step only declares the OEM policies that selinux-bounded-su-nxp reads.
    static Map<String, Path> donors(Path work) throws Exception {
        Path donor = work.resolve("oem-policy.cil");

        Files.writeString(donor, "");

        return Map.of("original:PlatSepolicy", donor, "original:VendorSepolicy", donor);
    }

    static void checkPolicyMember(
            Path root, Path input, Path work, Target target, String name, Path patchedPolicy)
            throws Exception {
        String member =
                name.equals("selinux-canbus-nxp")
                        ? "plat_sepolicy.cil"
                        : name.equals("selinux-service-contexts-nxp")
                                ? "plat_service_contexts"
                                : "plat_and_mapping_sepolicy.cil.sha256";

        target.output = work.resolve(name);
        Files.copy(
                input.resolve(member),
                target.output,
                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        target.uses =
                name.equals("selinux-hash-nxp")
                        ? Map.of("patched:selinux-canbus-nxp", patchedPolicy)
                        : name.equals("selinux-canbus-nxp") ? donors(work) : Map.of();
        apply(root, name, target);
        String good = Files.readString(target.output);

        if (name.contains("contexts")) {
            check(
                    good.equals(
                            Files.readString(input.resolve(member))
                                    + "qg.canbus u:object_r:voyemu_canbus_service:s0\n"
                                    + "qg.key_manager"
                                    + " u:object_r:voyemu_nxp_key_manager_service:s0\n"
                                    + "qg.system_policy"
                                    + " u:object_r:voyemu_nxp_system_policy_service:s0\n"),
                    "Only qg.canbus and qg.key_manager and qg.system_policy contexts appended");
        }

        if (name.contains("hash")) {
            check(
                    good.equals(FilesSupport.hash(patchedPolicy) + "\n"),
                    "Identifier derives from actual patched policy");
        }

        boolean denied = false;

        try {
            apply(root, name, target);
        } catch (Failure expected) {
            denied = true;
        }

        check(denied, "Reapplication rejected");
        rejectMutations(root, input, member, target, name);
        target.api = 28;
        target.profile = TestProfiles.SYNTHETIC;
    }

    static void rejectMutations(Path root, Path input, String member, Target target, String name)
            throws Exception {
        boolean denied;

        for (int mode = 1; mode < 3; mode++) {
            Files.copy(
                    input.resolve(member),
                    target.output,
                    java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            target.profile = TestProfiles.SYNTHETIC;
            target.api = mode == 1 ? 30 : 28;

            if (mode == 2) {
                Files.writeString(target.output, "changed\n");
            }

            var meta = metadata(root, name);
            denied = false;

            try {
                Recipes.apply(name + ".json", meta, target);
            } catch (Failure expected) {
                denied = true;
            }

            check(denied, "API/input/recipe mutation refused");
        }
    }
}
