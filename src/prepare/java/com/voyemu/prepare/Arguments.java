package com.voyemu.prepare;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

record Arguments(String command, String profile, Map<String, String> values, Set<String> flags) {
    private static final Set<String> VALUE =
            Set.of("--home", "--root", "--serial", "--input", "--lang", "--port");
    private static final Set<String> FLAG =
            Set.of(
                    "--json",
                    "--verbose",
                    "--quiet",
                    "--force",
                    "--rebuild",
                    "--with-cluster",
                    "--help",
                    "--version",
                    "--headless");
    private static final Set<String> COMMAND =
            Set.of("list", "check", "deps", "fetch", "build", "start", "stop", "status", "verify");

    static Arguments parse(String[] args) {
        Map<String, String> values = new LinkedHashMap<>();
        java.util.HashSet<String> flags = new java.util.HashSet<>();
        List<String> positions = new ArrayList<>();

        for (int i = 0; i < args.length; i++) {
            String arg = args[i];

            if (VALUE.contains(arg)) {
                Failure.require(
                        !values.containsKey(arg)
                                && i + 1 < args.length
                                && !args[i + 1].startsWith("--"),
                        "ARGUMENT_INVALID",
                        "Missing or duplicate value: " + arg,
                        2);
                values.put(arg, args[++i]);
            } else if (FLAG.contains(arg)) {
                Failure.require(flags.add(arg), "ARGUMENT_INVALID", "Duplicate flag: " + arg, 2);
            } else if (arg.startsWith("-")) {
                throw new Failure("ARGUMENT_INVALID", "Unknown option: " + arg, 2);
            } else {
                positions.add(arg);
            }
        }

        Failure.require(
                positions.size() <= 2, "ARGUMENT_INVALID", "Too many positional arguments", 2);
        String command = positions.isEmpty() ? "help" : positions.get(0);
        Failure.require(
                command.equals("help") || COMMAND.contains(command),
                "ARGUMENT_INVALID",
                "Unknown command: " + command,
                2);
        String profile = positions.size() == 2 ? positions.get(1) : null;

        if (profile != null) {
            ProfileRegistry.validId(profile);
        }

        Failure.require(
                !values.containsKey("--root") || !values.containsKey("--home"),
                "ARGUMENT_INVALID",
                "--root and --home cannot be combined",
                2);

        if (flags.contains("--force")) {
            Failure.require(
                    command.equals("fetch"), "ARGUMENT_INVALID", "--force requires fetch", 2);
        }

        if (flags.contains("--rebuild")) {
            Failure.require(
                    command.equals("build"), "ARGUMENT_INVALID", "--rebuild requires build", 2);
        }

        if (values.containsKey("--serial") || values.containsKey("--input")) {
            Failure.require(
                    command.equals("fetch"),
                    "ARGUMENT_INVALID",
                    "Input selection requires fetch",
                    2);
        }

        Failure.require(
                !values.containsKey("--serial") || !values.containsKey("--input"),
                "ARGUMENT_INVALID",
                "Select a device or a local input directory",
                2);

        return new Arguments(command, profile, Map.copyOf(values), Set.copyOf(flags));
    }

    boolean flag(String name) {
        return flags.contains(name);
    }

    String value(String name) {
        return values.get(name);
    }

    boolean progress() {
        return !flag("--json") && (!flag("--quiet") || flag("--verbose"));
    }
}
