package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** Restores the same QG DEX using only the declared minimal OEM input tree. */
public final class MinimalFrameworkTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("ROOT PREVIOUS_ARTIFACT_RESULT FRESH_OUTPUT");
        }

        Path root = Path.of(args[0]).toRealPath();
        Map<String, Object> profile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/free-09-chinese-20260106-6.0.3/profile.json"))));
        Map<String, Object> definition = Json.obj(Json.obj(profile, "original"), "OEMFramework");
        Map<String, Object> pins = Json.obj(definition, "sha256");

        if (pins.size() != 51
                || Json.obj(profile, "original").containsKey("SpeechService")
                || Json.obj(profile, "original").containsKey("TtsService")) {
            throw new AssertionError("unused inputs remain required");
        }

        Path inputs =
                root.resolve(".local/profiles/free-09-chinese-20260106-6.0.3/originals/framework");
        Map<String, Path> files = FilesSupport.tree(inputs);

        if (!files.keySet().equals(pins.keySet())) {
            throw new AssertionError("minimal input inventory differs");
        }

        Path output = Path.of(args[2]).toAbsolutePath();

        if (Files.exists(output)) {
            throw new AssertionError("fresh output required");
        }

        Files.createDirectories(output);
        Path classpath = Files.createDirectory(output.resolve("classpath"));
        stageClasspath(files, pins, classpath);
        Map<String, Object> previous =
                Json.obj(Json.object(Json.parse(Files.readString(Path.of(args[1])))), "artifacts");

        for (String name : List.of("QGAPI", "QGMOD", "QGVehicle")) {
            Path restored = Files.createDirectory(output.resolve(name));
            NativeCore.framework(classpath, inputs.resolve(name + ".jar"), name, restored);
            String component =
                    Map.of(
                                    "QGAPI",
                                    "framework-qgapi",
                                    "QGMOD",
                                    "framework-qgmod",
                                    "QGVehicle",
                                    "framework-qgvehicle")
                            .get(name);
            String expected = Json.str(Json.obj(previous, "component:" + component), "sha256");

            if (!FilesSupport.hash(restored.resolve(name + ".jar")).equals(expected)) {
                throw new AssertionError("restored JAR differs: " + name);
            }
        }

        System.out.println("51 OEM inputs reproduce all three previous QG JAR SHA256 values");
    }

    static void stageClasspath(Map<String, Path> files, Map<String, Object> pins, Path classpath)
            throws Exception {
        for (var entry : files.entrySet()) {
            if (!FilesSupport.hash(entry.getValue()).equals(pins.get(entry.getKey()))) {
                throw new AssertionError("input SHA differs");
            }

            String file = entry.getValue().getFileName().toString();

            if (entry.getKey().startsWith("arm64/") && file.endsWith(".oat")) {
                Files.copy(entry.getValue(), classpath.resolve(file));
            }

            if (!entry.getKey().contains("/") && file.endsWith(".vdex")) {
                Files.write(
                        classpath.resolve(file),
                        BootVdex.decode(Files.readAllBytes(entry.getValue())));
            }
        }
    }
}
