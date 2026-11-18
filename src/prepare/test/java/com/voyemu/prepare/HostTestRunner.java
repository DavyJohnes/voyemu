package com.voyemu.prepare;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/** Shared JDK and process operations for host-side tests. */
final class HostTestRunner {
    static int run(ProcessBuilder command, int timeoutSeconds)
            throws IOException, InterruptedException {
        Process process = command.start();

        try {
            require(
                    process.waitFor(timeoutSeconds, TimeUnit.SECONDS),
                    "Timed out after " + timeoutSeconds + " s: " + command.command());

            return process.exitValue();
        } finally {
            if (process.isAlive()) {
                process.destroyForcibly().onExit().join();
            }
        }
    }

    static Path jdkTool(String tool) {
        return jdkTool(Path.of(System.getProperty("java.home")), tool);
    }

    static Path jdkTool(Path jdk, String tool) {
        String executable =
                System.getProperty("os.name").startsWith("Windows") ? tool + ".exe" : tool;

        return jdk.resolve("bin").resolve(executable);
    }

    static Map<String, Path> pathOptions(String[] args, String... names) {
        var values = new HashMap<String, Path>();
        List<String> options = Arrays.asList(names);

        for (int index = 0; index < args.length; index++) {
            String[] parts = args[index].split("=", 2);
            String name = parts[0];

            if (!options.contains(name)) {
                throw new IllegalArgumentException("Unknown option: " + name);
            }

            if (parts.length == 1
                    && (index + 1 == args.length || args[index + 1].startsWith("--"))) {
                throw new IllegalArgumentException("Expected a path after " + name);
            }

            String value = parts.length == 2 ? parts[1] : args[++index];
            values.put(name, Path.of(value).toAbsolutePath().normalize());
        }

        for (String name : names) {
            if (!values.containsKey(name)) {
                throw new IllegalArgumentException("Missing option: " + name);
            }
        }

        return values;
    }

    static Path createJob(Path root, Path requested) throws IOException {
        Path jobs = root.toRealPath().resolve(".local/build/jobs");
        Path absolute = requested.toAbsolutePath();
        Path work = absolute.getRoot();

        for (Path part : absolute) {
            work = work.resolve(part).normalize();

            if (Files.exists(work, LinkOption.NOFOLLOW_LINKS)) {
                work = work.toRealPath();
            }
        }

        require(
                work.startsWith(jobs) && !work.equals(jobs),
                "Work directory must be under " + jobs);
        Files.createDirectories(work.getParent());

        return Files.createDirectory(work);
    }

    static List<Path> javaSources(Path dir) throws IOException {
        try (var paths = Files.walk(dir)) {
            return paths.filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().endsWith(".java"))
                    .sorted()
                    .toList();
        }
    }

    static List<Path> javaSources(Path dir, String glob) throws IOException {
        var matcher = dir.getFileSystem().getPathMatcher("glob:" + glob);

        try (var paths = Files.list(dir)) {
            return paths.filter(Files::isRegularFile)
                    .filter(path -> matcher.matches(path.getFileName()))
                    .sorted()
                    .toList();
        }
    }

    static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private HostTestRunner() {}
}
