package com.voyemu.prepare;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Pinned host tools for kernel builds: Zig-built GNU/OpenSSL sources and Android blobs. */
final class KernelHostTools {
    private static final long MAX_MEMBER_BYTES = 256L * 1024 * 1024;
    private static final long MAX_SOURCE_BYTES = 128L * 1024 * 1024;
    private static final int REGULAR = 0100644;
    private static final int EXECUTABLE = 0100755;
    private static final int SYMLINK = 0120000;

    /** Resolved host tool locations for one kernel build. */
    record Tools(
            String host,
            Path zig,
            Path make,
            Path sed,
            Path gzip,
            Path openssl,
            Path androidBuildTools) {}

    private KernelHostTools() {}

    /** NDK prebuilt host tag; macOS NDK binaries are universal. */
    static String host() {
        String os = System.getProperty("os.name").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch");

        if (os.startsWith("mac")) {
            return "darwin-x86_64";
        }

        if (os.startsWith("linux") && (arch.equals("amd64") || arch.equals("x86_64"))) {
            return "linux-x86_64";
        }

        throw new Failure(
                "KERNEL_HOST_UNSUPPORTED",
                "Kernel source builds run on macOS or Linux x86_64; other hosts use the published"
                        + " Image",
                2);
    }

    /**
     * Explicit Zig target for host programs. Without it Zig probes the exported Kbuild CC for a
     * native libc, which is a cross compiler or picks up the host system headers.
     */
    static String zigTarget() {
        String os = System.getProperty("os.name").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch").equals("aarch64") ? "aarch64" : "x86_64";

        return "-target " + arch + (os.startsWith("mac") ? "-macos" : "-linux-gnu");
    }

    static Map<String, Object> toolchain(Path project) throws Exception {
        return Json.object(Json.parse(Files.readString(project.resolve("src/app/toolchain.json"))));
    }

    static Path ndk(Path project) throws Exception {
        String version = Json.str(Json.obj(toolchain(project), "native"), "ndk");

        return Storage.confined(project, ".local/toolchains/native-sdk/ndk/" + version);
    }

    /** Installs missing pinned tools, then verifies every installed tool. */
    static Tools install(Path project, String host) throws Exception {
        var toolchain = toolchain(project);
        var optional = Json.obj(toolchain, "optionalTools");
        var build = Json.obj(optional, host.startsWith("darwin") ? "macosBuild" : "linuxBuild");
        Path zig =
                Storage.confined(project, Json.str(Json.obj(build, "zig"), "directory"))
                        .resolve("zig");

        require(Files.isExecutable(zig), "Pinned Zig missing: " + zig);
        Path make = ndk(project).resolve("prebuilt/" + host + "/bin/make");
        require(Files.isExecutable(make), "NDK GNU make missing: " + make);
        var kernel = Json.obj(optional, "kernelBuild");
        var sources = Json.obj(kernel, "sources");
        var directories = new LinkedHashMap<String, Path>();

        for (String name : List.of("sed", "gzip", "openssl")) {
            directories.put(name, source(project, name, Json.obj(sources, name), host, zig, make));
        }

        var android = Json.obj(kernel, "androidBuildTools");
        Path androidRoot = Storage.confined(project, Json.str(android, "directory"));
        var members = new ArrayList<>(Json.list(Json.obj(android, "hosts").get(host)));
        members.addAll(Json.list(android.get("common")));

        for (Object member : members) {
            member(androidRoot, Json.object(member), true);
        }

        Path bin = Storage.confined(androidRoot, "bin");
        Path toybox = Path.of("toybox");

        for (Object applet : Json.list(android.get("applets"))) {
            Path path = bin.resolve((String) applet);

            if (Files.notExists(path, LinkOption.NOFOLLOW_LINKS)) {
                Files.createSymbolicLink(path, toybox);
            }

            require(
                    Files.isSymbolicLink(path) && Files.readSymbolicLink(path).equals(toybox),
                    "Missing or changed toybox applet: " + applet);
        }

        // A missing shared library would otherwise make Kbuild fall back to the build time.
        var probe =
                new ProcessBuilder(bin.resolve("date").toString(), "-u", "-d", "@0", "+%s")
                        .redirectErrorStream(true)
                        .start();
        String epoch = new String(probe.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        require(
                probe.waitFor() == 0 && epoch.strip().equals("0"),
                "Pinned toybox does not run on this host: " + epoch.strip());

        return new Tools(
                host,
                zig,
                make,
                directories.get("sed"),
                directories.get("gzip"),
                directories.get("openssl"),
                androidRoot);
    }

    /** Verifies or fetches one selected-member manifest such as the API 30 compiler subset. */
    static void members(Path destination, Path manifest, boolean fetch) throws Exception {
        List<Object> members =
                Json.list(Json.object(Json.parse(Files.readString(manifest))).get("files"));

        for (Object member : members) {
            member(destination, Json.object(member), fetch);
        }

        System.out.println(
                "PASS " + manifest.getFileName() + ": " + members.size() + " immutable Git blobs");
    }

    private static Path source(
            Path project, String name, Map<String, Object> pin, String host, Path zig, Path make)
            throws Exception {
        Path root = Storage.confined(project, Json.str(pin, "directory"));
        Path receipt = root.resolve("voyemu-host-tool.json");
        var expected = new LinkedHashMap<String, Object>();

        expected.put("schema", 1);
        expected.put("name", name);
        expected.put("version", Json.str(pin, "version"));
        expected.put("source_sha256", Json.str(pin, "sha256"));
        expected.put("host", host);
        expected.put("zig_target", zigTarget());

        if (Files.isRegularFile(receipt)) {
            require(
                    Files.readString(receipt).equals(Json.write(expected) + "\n"),
                    "Installed host tool differs from its pin: " + root);

            return root;
        }

        require(
                Files.notExists(root, LinkOption.NOFOLLOW_LINKS),
                "Incomplete host tool directory; remove it to rebuild: " + root);
        Path archive =
                Downloads.cached(
                        pin,
                        Storage.confined(project, ".local/toolchains/downloads/sources"),
                        MAX_SOURCE_BYTES,
                        false);
        Path jobs = Storage.confined(project, ".local/build/jobs/kernel");
        Files.createDirectories(jobs);
        Path job = Files.createTempDirectory(jobs, "host-" + name + "-");

        try {
            command(job, null, "tar", "-xf", archive.toString(), "-C", job.toString());
            Path tree;

            try (var children = Files.list(job)) {
                var list = children.toList();
                require(list.size() == 1 && Files.isDirectory(list.get(0)), "Unexpected archive");
                tree = list.get(0);
            }

            var env = environment(project, zig, make);
            String cc = zig + " cc " + zigTarget();
            Path stage = job.resolve("install");

            switch (name) {
                case "sed", "gzip" -> {
                    command(
                            tree,
                            env,
                            "/bin/sh",
                            "./configure",
                            "--disable-nls",
                            "--prefix=" + stage,
                            "CC=" + cc,
                            "CFLAGS=-O2");
                    String program = name.equals("sed") ? "sed/sed" : "gzip";
                    // gzip generates headers only through its default goal.
                    command(
                            tree,
                            env,
                            make.toString(),
                            "SHELL=/bin/sh",
                            "-j2",
                            name.equals("sed") ? program : "all");
                    Files.createDirectories(stage.resolve("bin"));
                    Files.copy(tree.resolve(program), stage.resolve("bin").resolve(name));
                }
                case "openssl" -> {
                    command(
                            tree,
                            env,
                            "perl",
                            "Configure",
                            "no-shared",
                            "no-module",
                            "no-dso",
                            "no-asm",
                            "no-apps",
                            "no-docs",
                            "no-tests",
                            "--prefix=" + stage,
                            "--libdir=lib",
                            "CC=" + cc);
                    command(tree, env, make.toString(), "-j2", "build_libs");
                    command(tree, env, make.toString(), "install_dev");
                }
                default -> throw new Failure("KERNEL_HOST_TOOLS", "Unknown tool " + name, 3);
            }

            Path licenses = stage.resolve("licenses");
            Files.createDirectories(licenses);

            for (String license : List.of("COPYING", "LICENSE.txt")) {
                if (Files.isRegularFile(tree.resolve(license))) {
                    Files.copy(tree.resolve(license), licenses.resolve(license));
                }
            }

            Storage.atomic(stage.resolve("voyemu-host-tool.json"), Json.write(expected) + "\n");
            Files.createDirectories(root.getParent());
            Files.move(stage, root);
        } finally {
            FilesSupport.removeOwnedTree(job);
        }

        System.out.println("Built pinned host " + name + " " + Json.str(pin, "version"));

        return root;
    }

    /** Minimal environment for host-tool and kernel builds; host PATH tools stay last. */
    static Map<String, String> environment(Path project, Path zig, Path make) throws Exception {
        var env = new LinkedHashMap<String, String>();
        Path cache = Storage.confined(project, ".local/build/jobs/kernel/zig-cache");

        Files.createDirectories(cache);
        env.put("PATH", make.getParent() + ":/usr/bin:/bin:/usr/sbin:/sbin");
        env.put("LC_ALL", "C");
        env.put("TZ", "UTC");
        env.put("ZIG_GLOBAL_CACHE_DIR", cache.toString());
        env.put("ZIG_LOCAL_CACHE_DIR", cache.toString());
        String home = System.getenv("HOME");

        if (home != null) {
            env.put("HOME", home);
        }

        return env;
    }

    /**
     * Runs a tool with umask 022: extracted sources, copied headers and the embedded headers
     * archive record file modes, which must not follow the caller's umask.
     */
    static void command(Path directory, Map<String, String> env, String... args) throws Exception {
        var line = new ArrayList<>(List.of("/bin/sh", "-c", "umask 022 && exec \"$@\"", "sh"));

        line.addAll(List.of(args));
        var builder = new ProcessBuilder(line).inheritIO();

        if (directory != null) {
            builder.directory(directory.toFile());
        }

        if (env != null) {
            builder.environment().clear();
            builder.environment().putAll(env);
        }

        int status = builder.start().waitFor();
        require(status == 0, args[0] + " failed with status " + status);
    }

    private static void member(Path destination, Map<String, Object> pin, boolean fetch)
            throws Exception {
        String name = Json.str(pin, pin.containsKey("path") ? "path" : "file");

        require(!name.contains("\\"), "Unsafe tool member: " + name);

        for (String part : name.split("/", -1)) {
            require(
                    !part.isEmpty() && !part.equals(".") && !part.equals(".."),
                    "Unsafe tool member: " + name);
        }

        int slash = name.lastIndexOf('/');
        Path parent = Storage.confined(destination, slash == -1 ? "." : name.substring(0, slash));
        Path path = parent.resolve(name.substring(slash + 1));
        int mode =
                pin.containsKey("mode")
                        ? Math.toIntExact(Json.integer(pin, "mode"))
                        : name.startsWith("bin/") ? EXECUTABLE : REGULAR;
        require(
                mode == REGULAR || mode == EXECUTABLE || mode == SYMLINK,
                "Unsupported tool mode: " + name);

        if (matches(destination, path, mode, pin)) {
            return;
        }

        require(fetch, "Missing or changed pinned tool: " + path);
        require(
                Files.notExists(path, LinkOption.NOFOLLOW_LINKS),
                "Refusing to replace existing tool: " + path);
        Files.createDirectories(parent);
        Path stage = Files.createTempDirectory(parent, ".kernel-tool-");

        try {
            Path source = blob(pin, stage);
            require(filePins(source, pin), "Downloaded Git blob differs: " + name);

            if (mode == SYMLINK) {
                Path target = Path.of(Files.readString(source));
                require(
                        confinedLink(destination, path, target),
                        "Symlink escapes toolchain: " + name);
                Files.createSymbolicLink(path, target);
            } else {
                Files.setPosixFilePermissions(
                        source,
                        PosixFilePermissions.fromString(
                                mode == EXECUTABLE ? "rwxr-xr-x" : "rw-r--r--"));
                Files.createLink(path, source);
            }

            require(
                    matches(destination, path, mode, pin),
                    "Installed tool failed verification: " + name);
        } finally {
            FilesSupport.removeOwnedTree(stage);
        }
    }

    /** Gitiles rate-limits bursts of blob requests; retry those with a growing delay. */
    private static Path blob(Map<String, Object> pin, Path stage) throws Exception {
        for (int attempt = 1; ; attempt++) {
            try {
                return Downloads.cached(pin, stage, MAX_MEMBER_BYTES, true);
            } catch (Failure e) {
                if (!e.code.equals("DOWNLOAD_HTTP_FAILED") || attempt == 8) {
                    throw e;
                }

                Thread.sleep(15_000L * attempt);
            }
        }
    }

    private static boolean matches(Path destination, Path path, int mode, Map<String, Object> pin)
            throws Exception {
        if (mode == SYMLINK) {
            if (!Files.isSymbolicLink(path)) {
                return false;
            }

            Path target = Files.readSymbolicLink(path);
            byte[] bytes = target.toString().getBytes(StandardCharsets.UTF_8);

            return confinedLink(destination, path, target)
                    && pins(new ByteArrayInputStream(bytes), bytes.length, pin);
        }

        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) {
            return false;
        }

        boolean executable =
                PosixFilePermissions.toString(Files.getPosixFilePermissions(path)).contains("x");

        return executable == (mode == EXECUTABLE) && filePins(path, pin);
    }

    private static boolean confinedLink(Path destination, Path path, Path target) {
        Path resolved = path.getParent().resolve(target).normalize();

        return !resolved.equals(destination) && resolved.startsWith(destination);
    }

    private static boolean filePins(Path path, Map<String, Object> pin) throws Exception {
        try (InputStream input = Files.newInputStream(path)) {
            return pins(input, Files.size(path), pin);
        }
    }

    private static boolean pins(InputStream input, long size, Map<String, Object> pin)
            throws Exception {
        if (size > MAX_MEMBER_BYTES
                || (pin.containsKey("bytes") && size != Json.integer(pin, "bytes"))) {
            return false;
        }

        MessageDigest sha256 = MessageDigest.getInstance("SHA-256"),
                blob = MessageDigest.getInstance("SHA-1");
        blob.update(("blob " + size + "\0").getBytes(StandardCharsets.US_ASCII));
        byte[] buffer = new byte[128 * 1024];
        long read = 0;

        for (int count; (count = input.read(buffer)) != -1; ) {
            read += count;

            if (read > size) {
                return false;
            }

            sha256.update(buffer, 0, count);
            blob.update(buffer, 0, count);
        }

        HexFormat hex = HexFormat.of();

        return read == size
                && hex.formatHex(sha256.digest()).equals(Json.str(pin, "sha256"))
                && hex.formatHex(blob.digest()).equals(Json.str(pin, "git_blob"));
    }

    private static void require(boolean condition, String message) {
        Failure.require(condition, "KERNEL_HOST_TOOLS", message, 3);
    }
}
