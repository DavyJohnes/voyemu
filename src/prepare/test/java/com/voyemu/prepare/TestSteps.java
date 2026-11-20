package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Declared patch steps of one source profile, chosen by the value of a step flag. */
final class TestSteps {
    private TestSteps() {}

    /**
     * Merged constants of the steps NAMES.json of the first source profile whose first step
     * declares flag with value; the steps share the pin of one original.
     */
    static Map<String, Object> select(Path root, String flag, boolean value, String... names)
            throws Exception {
        try (var entries = Files.list(root.resolve("profiles"))) {
            for (Path directory : entries.sorted().toList()) {
                var chosen = constants(directory, names[0]);

                if (!chosen.containsKey(flag) || StepConstants.flag(chosen, flag) != value) {
                    continue;
                }

                var merged = new TreeMap<String, Object>();

                for (String name : names) {
                    merged.putAll(constants(directory, name));
                }

                return merged;
            }
        }

        throw new AssertionError("No step " + names[0] + ".json declares " + flag + " " + value);
    }

    /** Constants of the step NAME.json of every source profile that has it, in profile order. */
    static List<Map<String, Object>> declared(Path root, String name) throws Exception {
        var result = new ArrayList<Map<String, Object>>();

        try (var entries = Files.list(root.resolve("profiles"))) {
            for (Path directory : entries.sorted().toList()) {
                var constants = constants(directory, name);

                if (!constants.isEmpty()) {
                    result.add(constants);
                }
            }
        }

        return result;
    }

    /** The imported original FILE that the constants admit. */
    static Path original(Path root, Map<String, Object> constants, String file) throws Exception {
        String pin = StepConstants.pins(constants).get(0);

        return root.resolve(".local/profiles/" + TestProfiles.pinning(root, pin) + "/originals")
                .resolve(file);
    }

    private static Map<String, Object> constants(Path directory, String name) throws Exception {
        Path step = directory.resolve("patches").resolve(name + ".json");

        return Files.isRegularFile(step)
                ? Json.obj(Json.object(Json.parse(Files.readString(step))), "constants")
                : Map.of();
    }
}
