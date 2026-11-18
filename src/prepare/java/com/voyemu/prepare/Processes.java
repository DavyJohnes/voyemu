package com.voyemu.prepare;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

final class Processes {
    record Result(int exitCode, String stdout, String stderr) {}

    static Result run(
            Path executable,
            List<String> arguments,
            Path directory,
            Duration timeout,
            boolean allowFailure)
            throws Exception {
        String name =
                executable
                        .getFileName()
                        .toString()
                        .toLowerCase(java.util.Locale.ROOT)
                        .replace(".exe", "");

        Failure.require(
                !List.of("java", "javac", "node", "npm", "npx").contains(name),
                "HOST_RUNTIME_FORBIDDEN",
                "Native CLI cannot launch a JVM or Node runtime",
                3);
        Failure.require(
                executable.isAbsolute() && Files.isRegularFile(executable),
                "TOOL_MISSING",
                "Required native tool is missing: " + name,
                3);
        List<String> command = new ArrayList<>();
        command.add(executable.toString());
        command.addAll(arguments);
        ProcessBuilder builder = new ProcessBuilder(command);
        builder.environment().put("PYTHONDONTWRITEBYTECODE", "1");

        if (directory != null) {
            builder.directory(directory.toFile());
        }

        Process process = builder.start();
        var out = CompletableFuture.supplyAsync(() -> read(process.getInputStream()));
        var err = CompletableFuture.supplyAsync(() -> read(process.getErrorStream()));

        try {
            if (!process.waitFor(timeout.toMillis(), TimeUnit.MILLISECONDS)) {
                process.destroy();

                if (!process.waitFor(3, TimeUnit.SECONDS)) {
                    process.destroyForcibly();
                }

                throw new Failure("PROCESS_TIMEOUT", "Native tool timed out: " + name, 5);
            }

            Result result = new Result(process.exitValue(), out.get(), err.get());
            Failure.require(
                    allowFailure || result.exitCode() == 0,
                    "PROCESS_FAILED",
                    "Native tool failed: " + name + " (exit " + result.exitCode() + ")",
                    5);

            return result;
        } catch (InterruptedException e) {
            process.destroy();
            Thread.currentThread().interrupt();
            throw new Failure("ABORTED", "Operation interrupted", 130);
        } finally {
            if (process.isAlive()) {
                process.destroyForcibly();
            }

            process.getInputStream().close();
            process.getErrorStream().close();
            process.getOutputStream().close();
        }
    }

    private static String read(InputStream input) {
        try (input) {
            byte[] bytes = input.readNBytes(16 * 1024 * 1024 + 1);
            Failure.require(
                    bytes.length <= 16 * 1024 * 1024,
                    "PROCESS_OUTPUT_LIMIT",
                    "Native tool output exceeded its limit",
                    5);

            return new String(bytes, StandardCharsets.UTF_8);
        } catch (java.io.IOException e) {
            throw new Failure("PROCESS_READ_FAILED", "Could not read native tool output", 5);
        }
    }
}
