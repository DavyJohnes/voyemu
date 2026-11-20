package com.voyemu.prepare;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

/** Source profiles chosen by their declared data; tests never name a concrete profile ID. */
final class TestProfiles {
    /** A profile ID no source profile has; preparation decides by data, never by this ID. */
    static final String SYNTHETIC = "test-profile";

    private TestProfiles() {}

    /** ID of the first source profile whose patches declare the step NAME.json. */
    static String declaring(Path root, String name) throws IOException {
        try (var entries = Files.list(root.resolve("profiles"))) {
            for (Path directory : entries.sorted().toList()) {
                Path definition = directory.resolve("profile.json");

                if (!Files.isRegularFile(definition)) {
                    continue;
                }

                var data = Json.object(Json.parse(Files.readString(definition)));

                if (!(data.get("patches") instanceof java.util.Map<?, ?> patches)) {
                    continue;
                }

                for (Object node : patches.values()) {
                    if (node instanceof java.util.Map<?, ?> artifact
                            && artifact.get("steps") instanceof java.util.List<?> steps
                            && steps.contains(name + ".json")) {
                        return directory.getFileName().toString();
                    }
                }
            }
        }

        throw new AssertionError("No source profile declares " + name + ".json");
    }

    /** ID of the first source profile whose definition pins the SHA-256 of an original. */
    static String pinning(Path root, String sha256) throws IOException {
        try (var entries = Files.list(root.resolve("profiles"))) {
            for (Path directory : entries.sorted().toList()) {
                Path definition = directory.resolve("profile.json");

                if (Files.isRegularFile(definition)
                        && Files.readString(definition).contains("\"" + sha256 + "\"")) {
                    return directory.getFileName().toString();
                }
            }
        }

        throw new AssertionError("No source profile pins " + sha256);
    }

    /** Constants of the step NAME.json of the first source profile whose step admits the pin. */
    static Map<String, Object> constants(Path root, String name, String sha256) throws IOException {
        try (var entries = Files.list(root.resolve("profiles"))) {
            for (Path directory : entries.sorted().toList()) {
                Path step = directory.resolve("patches").resolve(name + ".json");

                if (!Files.isRegularFile(step)) {
                    continue;
                }

                var constants =
                        Json.obj(Json.object(Json.parse(Files.readString(step))), "constants");

                if (StepConstants.admits(constants, sha256)) {
                    return constants;
                }
            }
        }

        throw new AssertionError("No step " + name + ".json admits " + sha256);
    }

    /** Constants of the step NAME.json that admits the original file. */
    static Map<String, Object> constants(Path root, String name, Path original) throws Exception {
        return constants(root, name, FilesSupport.hash(original));
    }
}
