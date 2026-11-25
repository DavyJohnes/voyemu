package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class PreparationMainTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void checkVariant(Path root, Path recipe, Map<String, Object> original)
            throws Exception {
        String parent = recipe.getFileName().toString(), id = "test-profile-x86_64";
        Map<String, Object> definition = new LinkedHashMap<>(original);

        definition.putAll(
                Map.of(
                        "id",
                        parent,
                        "platform",
                        "passion",
                        "board",
                        "8155",
                        "original",
                        Map.of(),
                        "verify",
                        Map.of("services", List.of("stock.service")),
                        "android",
                        Map.of(
                                "package",
                                "system-images;android-30;default;arm64-v8a",
                                "features",
                                Map.of("VirtioInput", "on")),
                        "emulator",
                        Map.of("memory", 2048)));
        Files.writeString(recipe.resolve("profile.json"), Json.write(definition));
        Path directory = Files.createDirectory(root.resolve("profiles/" + id));
        Files.writeString(
                directory.resolve("profile.json"),
                Json.write(
                        Map.of(
                                "id",
                                id,
                                "schema",
                                1,
                                "title",
                                "Test vehicle",
                                "status",
                                "incomplete",
                                "features",
                                Map.of(
                                        "mcu_property_runtime",
                                        false,
                                        "display_power_runtime",
                                        false),
                                "emulator",
                                Map.of(
                                        "acceleration",
                                        "kvm",
                                        "gpuMode",
                                        "host",
                                        "memory",
                                        4096,
                                        "audioOutput",
                                        true),
                                "variant",
                                Map.of("recipeProfile", parent, "androidBase", "test-base"))));
        Path descriptor =
                Files.createDirectories(root.resolve("profiles/common/android-bases"))
                        .resolve("test-base.json");
        check(
                Artifacts.nativeComponentAbi(Map.of("abi", "arm64-v8a"), "x86_64")
                                .equals("arm64-v8a")
                        && Artifacts.nativeComponentAbi(Map.of(), "x86_64").equals("x86_64"),
                "OEM library must match the consuming executable");
        var base =
                Map.of(
                        "package",
                        "system-images;android-30;default;x86_64",
                        "clean_base",
                        Map.of(),
                        "features",
                        Map.of("MultiDisplay", "on"));
        Files.writeString(descriptor, Json.write(base));
        var registry = new ProfileRegistry(root, id);
        var variant = registry.get(id);
        var effective = variant.data();
        check(
                registry.sourceDirectory(variant).equals(recipe)
                        && variant.referenceProfile().equals(parent),
                "variant recipe path");
        check(
                Json.obj(Json.obj(effective, "android"), "features")
                                .equals(Map.of("VirtioInput", "on", "MultiDisplay", "on"))
                        && Json.integer(Json.obj(effective, "emulator"), "memory") == 4096
                        && Boolean.TRUE.equals(Json.obj(effective, "emulator").get("audioOutput"))
                        && Json.obj(effective, "features")
                                .equals(
                                        Map.of(
                                                "mcu_property_runtime",
                                                false,
                                                "display_power_runtime",
                                                false))
                        && Json.obj(effective, "original").isEmpty(),
                "variant resolved Android and vehicle settings");
        String payload = Json.write(effective) + "\n";
        var request =
                new LinkedHashMap<String, Object>(
                        Map.of(
                                "schema",
                                1,
                                "operation",
                                "plan",
                                "root",
                                root.toString(),
                                "profile",
                                id,
                                "profile_sha256",
                                NativeCore.hash(
                                        payload.getBytes(java.nio.charset.StandardCharsets.UTF_8)),
                                "resolved_profile",
                                payload));
        check(PreparationMain.run(request).get("profile").equals(id), "variant plan");
        Files.writeString(
                descriptor,
                Json.write(
                        Map.of(
                                "package",
                                "system-images;android-30;default;x86_64",
                                "clean_base",
                                Map.of("sha", "changed"))));
        boolean rejected = false;

        try {
            registry.verifySource(variant);
        } catch (Failure failure) {
            rejected = failure.code.equals("PROFILE_RESOURCE_MODIFIED");
        }

        check(rejected, "change to SDK descriptor escaped source check");
        rejected = false;

        try {
            PreparationMain.run(request);
        } catch (Failure failure) {
            rejected = failure.code.equals("PREPARATION_PROFILE_PIN");
        }

        check(rejected, "stale effective descriptor accepted");
        checkTarget(root, recipe, definition, descriptor, base, request);
    }

    private static void checkTarget(
            Path root,
            Path recipe,
            Map<String, Object> definition,
            Path descriptor,
            Map<String, ?> base,
            Map<String, Object> request)
            throws Exception {
        String parent = recipe.getFileName().toString();

        definition.put(
                "targets",
                Map.of(
                        "arm", Map.of("guestAbi", "arm64-v8a", "preparation", "sdk"),
                        "x86",
                                Map.of(
                                        "guestAbi", "x86_64",
                                        "preparation", "signed-offline",
                                        "androidBase", "test-base",
                                        "emulator",
                                                Map.of("acceleration", "kvm", "gpuMode", "host"))));
        Files.writeString(recipe.resolve("profile.json"), Json.write(definition));
        var pinnedBase = new LinkedHashMap<String, Object>(base);
        pinnedBase.put("sha256", "1".repeat(64));
        Files.writeString(descriptor, Json.write(pinnedBase));
        var targetRegistry = new ProfileRegistry(root, parent, "x86");
        var selected = targetRegistry.get(parent).data();
        check(
                targetRegistry.sourceDirectory(targetRegistry.get(parent)).equals(recipe)
                        && selected.get("original").equals(definition.get("original")),
                "resolved target retains the canonical OEM source");
        var targetIdentity = Map.of("name", "x86", "abi", "x86_64", "base_sha256", "1".repeat(64));
        var resolved = Json.write(selected);
        request.put("profile", parent);
        request.put("resolved_profile", resolved);
        request.put(
                "profile_sha256",
                NativeCore.hash(resolved.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
        request.put("target", targetIdentity);
        request.put("android_base", Json.obj(selected, "android"));
        check(PreparationMain.run(request).get("target").equals(targetIdentity), "target plan");
        request.put("android_base", Json.obj(definition, "android"));
        boolean rejected = false;

        try {
            PreparationMain.run(request);
        } catch (Failure failure) {
            rejected = failure.code.equals("PREPARATION_PROFILE_PIN");
        }

        check(rejected, "selected ABI cannot reuse another target's Android base");
    }

    public static void main(String[] args) throws Exception {
        Path project =
                Path.of(System.getProperty("voyemu.project.root", System.getProperty("user.dir")))
                        .toRealPath();

        check(
                Files.isDirectory(project.resolve("src/prepare"))
                        && Files.isDirectory(project.resolve("profiles")),
                "explicit Voyemu project root required");
        Path parent = project.resolve(".local/build/tmp");
        Storage.directory(parent);
        Path root = Files.createTempDirectory(parent, "voyemu-preparation-main-");

        try {
            String id = "test-profile";
            Path profile = root.resolve("profiles/" + id);
            Files.createDirectories(profile.resolve("patches"));
            Path step = profile.resolve("patches/test.json");
            Files.writeString(
                    step,
                    Json.write(Map.of("schema", 1, "constants", Map.of(), "calls", List.of()))
                            + "\n");
            Map<String, Object> definition =
                    Map.of(
                            "schema",
                            1,
                            "components",
                            Map.of("sample", "components/sample"),
                            "patches",
                            Map.of("sample", Map.of("steps", List.of("test.json"))),
                            "install",
                            List.of());
            Path json = profile.resolve("profile.json");
            Files.writeString(json, Json.write(definition));
            Map<String, Object> request =
                    new LinkedHashMap<>(
                            Map.of(
                                    "schema",
                                    1,
                                    "operation",
                                    "plan",
                                    "root",
                                    root.toString(),
                                    "profile",
                                    id,
                                    "profile_sha256",
                                    FilesSupport.hash(json)));
            Map<String, Object> result = PreparationMain.run(request);
            check(
                    Boolean.FALSE.equals(result.get("machine_ready"))
                            && Boolean.FALSE.equals(result.get("guest_started")),
                    "plan falsely reports machine readiness");
            check(
                    !result.containsKey("step_source_pins") && !result.containsKey("revision"),
                    "plan persists authoring source pins");
            check(!Files.exists(root.resolve(".local")), "read-only plan created runtime data");
            ProfileRegistry registry = new ProfileRegistry(root);
            ProfileRegistry.Profile selected = registry.get(id);
            check(
                    registry.sourceDirectory(selected).equals(profile),
                    "preparation did not use the root source profile directly");
            check(
                    !Files.exists(root.resolve(".local")),
                    "source profile resolution created a copied snapshot");
            Files.writeString(step, "changed\n");
            boolean refused = false;

            try {
                registry.sourceDirectory(selected);
            } catch (Failure error) {
                refused = error.code.equals("PROFILE_RESOURCE_MODIFIED");
            }

            check(refused, "changed source profile was accepted for preparation");
            request.put("profile_sha256", "0".repeat(64));
            refused = false;

            try {
                PreparationMain.run(request);
            } catch (Failure error) {
                refused = error.code.equals("PREPARATION_PROFILE_PIN");
            }

            check(refused, "profile request pin mismatch accepted");
            request.put("operation", "start");
            refused = false;

            try {
                PreparationMain.run(request);
            } catch (Failure error) {
                refused = error.code.equals("PREPARATION_OPERATION");
            }

            check(refused, "guest launch accepted by preparation helper");
            checkVariant(root, profile, definition);
            Path separatePlatform = root.resolve("java-sdk/android.jar");
            Storage.directory(separatePlatform.getParent());
            Files.writeString(separatePlatform, "pinned platform fixture");
            Storage storage =
                    new Storage(
                            root.resolve(".local"),
                            root.resolve(".local/toolchains"),
                            root.resolve(".local/toolchains/downloads"),
                            root.resolve(".local/build"),
                            root.resolve(".local/build/tmp"),
                            root.resolve(".local/build/logs"));
            Path sdk = root.resolve("native-sdk");
            Toolchains explicit =
                    new Toolchains(storage, Map.of(), sdk, null, null, separatePlatform);
            check(
                    explicit.platform().equals(separatePlatform) && explicit.sdk.equals(sdk),
                    "explicit Java platform changed native SDK layout");
            check(!Files.exists(sdk), "platform override mutated native SDK");
            RuntimeException diagnostic =
                    new RuntimeException("secret sentinel must not be logged");
            StackTraceElement[] frames = new StackTraceElement[30];

            for (int i = 0; i < frames.length; i++) {
                frames[i] = new StackTraceElement("Example", "method", "Example.java", i);
            }

            diagnostic.setStackTrace(frames);
            java.io.ByteArrayOutputStream captured = new java.io.ByteArrayOutputStream();
            PreparationMain.diagnostic(diagnostic, new java.io.PrintStream(captured));
            String trace = captured.toString(java.nio.charset.StandardCharsets.UTF_8);
            check(
                    !trace.contains("secret sentinel")
                            && trace.contains("truncated")
                            && trace.lines().filter(line -> line.startsWith("  at ")).count() == 24,
                    "diagnostic leaked message or exceeded stack bound");
            System.out.println(
                    "explicit source registry, separate Java platform and preparation request"
                            + " guards passed");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }
    }
}
