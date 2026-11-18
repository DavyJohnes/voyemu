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
