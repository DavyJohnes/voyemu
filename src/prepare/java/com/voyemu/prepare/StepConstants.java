package com.voyemu.prepare;

import java.util.List;
import java.util.Map;

/**
 * The constants of one declared patch step: the pins of the originals it accepts and the values
 * that differ between firmware versions. A guard takes them from the step, never from a profile ID.
 */
final class StepConstants {
    static final String PINS = "original_sha256_allowlist";

    private StepConstants() {}

    /** The SHA-256 pins of the originals the step accepts; empty when it declares none. */
    static List<String> pins(Map<String, Object> constants) {
        return constants.containsKey(PINS) ? texts(constants, PINS) : List.of();
    }

    /** True when the step accepts the original with this SHA-256. */
    static boolean admits(Map<String, Object> constants, String sha256) {
        return pins(constants).contains(sha256);
    }

    static boolean flag(Map<String, Object> constants, String name) {
        Failure.require(
                constants.get(name) instanceof Boolean,
                "PATCH_SOURCE",
                "Step constant " + name + " must be a boolean",
                3);

        return (Boolean) constants.get(name);
    }

    static String text(Map<String, Object> constants, String name) {
        Failure.require(
                constants.get(name) instanceof String,
                "PATCH_SOURCE",
                "Step constant " + name + " must be a string",
                3);

        return (String) constants.get(name);
    }

    static List<String> texts(Map<String, Object> constants, String name) {
        Failure.require(
                constants.get(name) instanceof List<?> values
                        && values.stream().allMatch(String.class::isInstance),
                "PATCH_SOURCE",
                "Step constant " + name + " must be a list of strings",
                3);

        return Json.list(constants.get(name)).stream().map(String.class::cast).toList();
    }
}
