package com.voyemu.prepare;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;

/** Read-only resolution; directories are created only by mutating operations. */
record Storage(Path data, Path config, Path cache, Path state, Path run, Path logs) {
    static Storage resolve(String explicitHome, Map<String, String> env, String os, Path userHome) {
        userHome = canonicalRoot(userHome);
        String selected = explicitHome != null ? explicitHome : env.get("VOYEMU_HOME");

        if (selected != null) {
            Path home = Path.of(selected);
            Failure.require(
                    home.isAbsolute(),
                    "HOME_INVALID",
                    "--home and VOYEMU_HOME must be absolute",
                    2);
            home = canonicalRoot(home);

            return new Storage(
                    home.resolve("data"),
                    home.resolve("config"),
                    home.resolve("cache"),
                    home.resolve("state"),
                    home.resolve("run"),
                    home.resolve("logs"));
        }

        if (os.contains("mac") || os.contains("darwin")) {
            Path support = userHome.resolve("Library/Application Support/voyemu");
            Path cache = userHome.resolve("Library/Caches/voyemu");
            Path temp = absolute(env.get("TMPDIR"), userHome.resolve("Library/Caches/voyemu/tmp"));

            return new Storage(
                    support.resolve("data"),
                    support.resolve("config"),
                    cache,
                    support.resolve("state"),
                    temp.resolve("voyemu-run"),
                    userHome.resolve("Library/Logs/voyemu"));
        }

        if (os.contains("windows")) {
            Path local =
                    absolute(env.get("LOCALAPPDATA"), userHome.resolve("AppData/Local"))
                            .resolve("Voyemu");
            Path roaming =
                    absolute(env.get("APPDATA"), userHome.resolve("AppData/Roaming"))
                            .resolve("Voyemu");

            return new Storage(
                    local.resolve("Data"),
                    roaming,
                    local.resolve("Cache"),
                    local.resolve("State"),
                    local.resolve("Run"),
                    local.resolve("Logs"));
        }

        Path data =
                absolute(env.get("XDG_DATA_HOME"), userHome.resolve(".local/share"))
                        .resolve("voyemu");
        Path config =
                absolute(env.get("XDG_CONFIG_HOME"), userHome.resolve(".config")).resolve("voyemu");
        Path cache =
                absolute(env.get("XDG_CACHE_HOME"), userHome.resolve(".cache")).resolve("voyemu");
        Path state =
                absolute(env.get("XDG_STATE_HOME"), userHome.resolve(".local/state"))
                        .resolve("voyemu");
        Path runtime = absolute(env.get("XDG_RUNTIME_DIR"), cache.resolve("run")).resolve("voyemu");

        return new Storage(data, config, cache, state, runtime, state.resolve("logs"));
    }

    private static Path absolute(String value, Path fallback) {
        return canonicalRoot(
                value != null && Path.of(value).isAbsolute() ? Path.of(value) : fallback);
    }

    /** Canonicalize the user-selected root before creating managed descendants. */
    private static Path canonicalRoot(Path input) {
        Path selected = input.toAbsolutePath().normalize();
        Path existing = selected;

        while (existing != null && !Files.exists(existing)) {
            existing = existing.getParent();
        }

        if (existing == null) {
            throw new Failure("HOME_INVALID", "No existing root directory", 2);
        }

        try {
            return existing.toRealPath().resolve(existing.relativize(selected));
        } catch (IOException e) {
            throw new Failure("HOME_INVALID", "Could not resolve data root", 2);
        }
    }

    Map<String, Object> json() {
        Map<String, Object> out = new LinkedHashMap<>();

        out.put("data", data.toString());
        out.put("config", config.toString());
        out.put("cache", cache.toString());
        out.put("state", state.toString());
        out.put("run", run.toString());
        out.put("logs", logs.toString());

        return out;
    }

    Path originals(String id) {
        return data.resolve("profiles").resolve(id).resolve("originals");
    }

    Path machine(String id) {
        return data.resolve("machines").resolve(id);
    }

    Path recipe(String id) {
        return data.resolve("profiles").resolve(id);
    }

    Path sdk() {
        return data.resolve("toolchains/native-sdk");
    }

    static Path confined(Path base, String relative) {
        Path part = Path.of(relative);

        Failure.require(
                !part.isAbsolute() && !relative.contains("\\"),
                "PATH_INVALID",
                "Unsafe relative path",
                2);

        for (Path segment : part) {
            Failure.require(!segment.toString().equals(".."), "PATH_INVALID", "Path traversal", 2);
        }

        Path normalized = base.toAbsolutePath().normalize();
        Path target = normalized.resolve(part).normalize();
        Failure.require(
                target.startsWith(normalized), "PATH_INVALID", "Path leaves data directory", 2);
        Path probe = target;

        while (probe != null) {
            Failure.require(
                    !Files.isSymbolicLink(probe),
                    "PATH_SYMLINK",
                    "Managed paths cannot contain symbolic links",
                    2);
            probe = probe.getParent();
        }

        return target;
    }

    static void directory(Path path) throws IOException {
        confined(path, ".");
        Files.createDirectories(path);

        try {
            Files.setPosixFilePermissions(path, PosixFilePermissions.fromString("rwx------"));
        } catch (UnsupportedOperationException ignored) {
            /* Windows inherits the user's directory ACL. */
        }
    }

    static void atomic(Path target, String text) throws IOException {
        directory(target.getParent());
        Failure.require(!Files.isSymbolicLink(target), "PATH_SYMLINK", "Refusing symbolic link", 2);
        Path temp =
                target.resolveSibling(
                        "." + target.getFileName() + "." + UUID.randomUUID() + ".tmp");

        try {
            Files.writeString(temp, text);

            try {
                Files.setPosixFilePermissions(temp, PosixFilePermissions.fromString("rw-------"));
            } catch (UnsupportedOperationException ignored) {
                /* Windows inherits the user's directory ACL. */
            }

            Files.move(
                    temp,
                    target,
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(temp);
        }
    }
}
