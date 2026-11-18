package com.voyemu.prepare;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermission;
import java.security.MessageDigest;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.spi.ToolProvider;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.GZIPInputStream;
import java.util.zip.ZipInputStream;

/**
 * Recipe of the java-runtime release assets: links the pinned modules of src/app/toolchain.json
 * javaRuntime from the pinned Temurin jmods of each host with the jlink of the same JDK and writes
 * one deterministic ZIP per host. Runs on the pinned JDK of the developer tools.
 */
public final class JavaRuntimeBuild {
    static final List<String> HOSTS = List.of("darwin-arm64", "linux-x64", "win32-x64");
    // MS-DOS date 1980-01-01, time 00:00:00: the earliest ZIP timestamp.
    private static final int ZIP_DATE = (1 << 5) | 1;

    private static void need(boolean value, String message) {
        if (!value) {
            throw new IllegalStateException("JAVA_RUNTIME_BUILD: " + message);
        }
    }

    private static String sha(Path path) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");

        try (InputStream in = Files.newInputStream(path)) {
            byte[] buffer = new byte[1 << 16];

            for (int n; (n = in.read(buffer)) >= 0; ) {
                digest.update(buffer, 0, n);
            }
        }

        return HexFormat.of().formatHex(digest.digest());
    }

    private static Map<String, Object> pins(Path root) throws Exception {
        return Json.obj(
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json")))),
                "javaRuntime");
    }

    static List<String> modules(Map<String, Object> pins) {
        List<String> modules = new ArrayList<>();

        for (Object item : Json.list(pins.get("modules"))) {
            need(
                    item instanceof String name
                            && name.matches("[a-z][a-z0-9]*(?:\\.[a-z][a-z0-9]*)+")
                            && !modules.contains(name),
                    "unique module names required");
            modules.add((String) item);
        }

        need(modules.contains("java.base"), "java.base required");

        return modules;
    }

    private static Path jmods(Map<String, Object> pins, String host, Path downloads)
            throws Exception {
        Map<String, Object> spec = Json.obj(Json.obj(pins, "jmods"), host);
        String url = Json.str(spec, "url"), expected = Json.str(spec, "sha256");

        need(expected.matches("[0-9a-f]{64}") && url.startsWith("https://"), "jmods pin " + host);
        Path archive = downloads.resolve(url.substring(url.lastIndexOf('/') + 1));

        if (!Files.exists(archive, LinkOption.NOFOLLOW_LINKS)) {
            Files.createDirectories(downloads);
            Path partial = downloads.resolve(archive.getFileName() + ".partial");
            HttpClient client =
                    HttpClient.newBuilder()
                            .followRedirects(HttpClient.Redirect.NORMAL)
                            .connectTimeout(Duration.ofMinutes(1))
                            .build();
            System.err.println("Fetching " + url);
            HttpResponse<Path> response =
                    client.send(
                            HttpRequest.newBuilder(URI.create(url))
                                    .timeout(Duration.ofMinutes(10))
                                    .build(),
                            HttpResponse.BodyHandlers.ofFile(partial));
            need(response.statusCode() == 200, "jmods download status " + response.statusCode());
            need(sha(partial).equals(expected), "downloaded jmods differ from pin " + host);
            Files.move(partial, archive, StandardCopyOption.ATOMIC_MOVE);
        }

        need(
                Files.isRegularFile(archive, LinkOption.NOFOLLOW_LINKS)
                        && sha(archive).equals(expected),
                "jmods archive differs from pin " + host);

        return archive;
    }

    // Writes only the *.jmod members of the archive, flat, into a fresh directory.
    private static void extractJmods(Path archive, Path output) throws Exception {
        Files.createDirectories(output);
        String name = archive.getFileName().toString();

        if (name.endsWith(".zip")) {
            try (ZipInputStream zip = new ZipInputStream(Files.newInputStream(archive))) {
                for (var entry = zip.getNextEntry(); entry != null; entry = zip.getNextEntry()) {
                    if (!entry.isDirectory() && entry.getName().endsWith(".jmod")) {
                        writeJmod(output, entry.getName(), zip);
                    }
                }
            }
        } else {
            need(name.endsWith(".tar.gz"), "tar.gz or zip jmods archive required");

            try (InputStream tar = new GZIPInputStream(Files.newInputStream(archive), 1 << 16)) {
                byte[] header = new byte[512];
                String longName = null;

                while (tar.readNBytes(header, 0, 512) == 512 && header[0] != 0) {
                    String entry = text(header, 0, 100), prefix = text(header, 345, 155);
                    long size = Long.parseLong(text(header, 124, 12).trim(), 8);
                    byte type = header[156];
                    byte[] body = tar.readNBytes((int) Math.min(size, Integer.MAX_VALUE));
                    need(body.length == size, "truncated tar member");
                    tar.skipNBytes((512 - size % 512) % 512);

                    if (type == 'L') {
                        longName = new String(body, StandardCharsets.UTF_8).replace("\0", "");
                        continue;
                    }

                    String path =
                            longName != null
                                    ? longName
                                    : prefix.isEmpty() ? entry : prefix + "/" + entry;
                    longName = null;

                    if ((type == '0' || type == 0) && path.endsWith(".jmod")) {
                        writeJmod(output, path, new java.io.ByteArrayInputStream(body));
                    }
                }
            }
        }
    }

    private static String text(byte[] header, int offset, int length) {
        int end = offset;

        while (end < offset + length && header[end] != 0) {
            end++;
        }

        return new String(header, offset, end - offset, StandardCharsets.UTF_8);
    }

    private static void writeJmod(Path output, String member, InputStream in) throws Exception {
        String file = member.substring(member.lastIndexOf('/') + 1);

        need(file.matches("[a-z][a-z0-9.]*\\.jmod"), "unexpected jmod member " + member);
        Path target = output.resolve(file);
        need(!Files.exists(target), "duplicate jmod " + file);
        Files.copy(in, target);
    }

    private static void run(String tool, List<String> arguments) {
        ToolProvider provider =
                ToolProvider.findFirst(tool).orElseThrow(() -> new IllegalStateException(tool));
        StringWriter out = new StringWriter();
        int status =
                provider.run(
                        new PrintWriter(out),
                        new PrintWriter(out),
                        arguments.toArray(String[]::new));

        System.err.print(out);
        need(status == 0, tool + " failed");
    }

    /** Links one host runtime and writes java-runtime-HOST.zip; returns the archive. */
    static Path build(Map<String, Object> pins, String host, Path downloads, Path work)
            throws Exception {
        Path archive = jmods(pins, host, downloads), modules = work.resolve(host + "-jmods");

        extractJmods(archive, modules);
        Path image = work.resolve(host + "/java-runtime");
        Files.createDirectories(image.getParent());
        // Only the Java debug attributes are stripped: native stripping needs the host objcopy,
        // which cannot read the Mach-O and PE files of the foreign hosts.
        run(
                "jlink",
                List.of(
                        "--module-path",
                        modules.toString(),
                        "--add-modules",
                        String.join(",", modules(pins)),
                        "--strip-java-debug-attributes",
                        // These two jlink plugins can run in either order and change the generated files.
                        "--disable-plugin",
                        "generate-jli-classes",
                        "--no-header-files",
                        "--no-man-pages",
                        "--output",
                        image.toString()));

        return image;
    }

    // Deterministic ZIP of a directory under its own name: sorted entries, the earliest DOS time,
    // Unix modes 755/644, symbolic links as links, deflate level 9.
    static void zip(Path tree, Path output) throws Exception {
        List<Path> paths;

        try (var walk = Files.walk(tree)) {
            paths =
                    walk.filter(p -> !p.equals(tree))
                            .sorted(Comparator.comparing(p -> tree.relativize(p).toString()))
                            .toList();
        }

        ByteArrayOutputStream central = new ByteArrayOutputStream();
        int count = 0;

        try (OutputStream out = Files.newOutputStream(output)) {
            long offset = 0;

            for (Path path : paths) {
                String name =
                        tree.getFileName()
                                + "/"
                                + tree.relativize(path).toString().replace('\\', '/');
                boolean link = Files.isSymbolicLink(path),
                        directory = !link && Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS);
                byte[] data =
                        link
                                ? Files.readSymbolicLink(path)
                                        .toString()
                                        .replace('\\', '/')
                                        .getBytes(StandardCharsets.UTF_8)
                                : directory ? new byte[0] : Files.readAllBytes(path);
                int mode =
                        link
                                ? 0120777
                                : directory
                                        ? 040755
                                        : Files.getPosixFilePermissions(path)
                                                        .contains(PosixFilePermission.OWNER_EXECUTE)
                                                ? 0100755
                                                : 0100644;

                if (directory) {
                    name += "/";
                }

                CRC32 crc = new CRC32();
                crc.update(data);
                byte[] stored = data;
                int method = 0;

                if (!link && data.length > 0) {
                    Deflater deflater = new Deflater(9, true);
                    deflater.setInput(data);
                    deflater.finish();
                    ByteArrayOutputStream packed = new ByteArrayOutputStream();
                    byte[] buffer = new byte[1 << 16];

                    while (!deflater.finished()) {
                        packed.write(buffer, 0, deflater.deflate(buffer));
                    }

                    deflater.end();
                    stored = packed.toByteArray();
                    method = 8;
                }

                byte[] encoded = name.getBytes(StandardCharsets.UTF_8);
                need(offset + stored.length < 0xFFFFFFFFL && count < 0xFFFF, "ZIP64 not needed");
                ByteArrayOutputStream local = new ByteArrayOutputStream();
                le(local, 0x04034b50, 4);
                le(local, 20, 2);
                le(local, 0x0800, 2);
                le(local, method, 2);
                le(local, 0, 2);
                le(local, ZIP_DATE, 2);
                le(local, crc.getValue(), 4);
                le(local, stored.length, 4);
                le(local, data.length, 4);
                le(local, encoded.length, 2);
                le(local, 0, 2);
                local.write(encoded);
                out.write(local.toByteArray());
                out.write(stored);
                le(central, 0x02014b50, 4);
                le(central, (3 << 8) | 20, 2);
                le(central, 20, 2);
                le(central, 0x0800, 2);
                le(central, method, 2);
                le(central, 0, 2);
                le(central, ZIP_DATE, 2);
                le(central, crc.getValue(), 4);
                le(central, stored.length, 4);
                le(central, data.length, 4);
                le(central, encoded.length, 2);
                le(central, 0, 2);
                le(central, 0, 2);
                le(central, 0, 2);
                le(central, 0, 2);
                le(central, ((long) mode << 16) | (directory ? 0x10 : 0), 4);
                le(central, offset, 4);
                central.write(encoded);
                offset += local.size() + stored.length;
                count++;
            }

            byte[] directory = central.toByteArray();
            out.write(directory);
            ByteArrayOutputStream end = new ByteArrayOutputStream();
            le(end, 0x06054b50, 4);
            le(end, 0, 4);
            le(end, count, 2);
            le(end, count, 2);
            le(end, directory.length, 4);
            le(end, offset, 4);
            le(end, 0, 2);
            out.write(end.toByteArray());
        }
    }

    private static void le(ByteArrayOutputStream out, long value, int bytes) {
        for (int i = 0; i < bytes; i++) {
            out.write((int) (value >>> (8 * i)) & 0xff);
        }
    }

    private static void remove(Path path) throws IOException {
        if (Files.exists(path, LinkOption.NOFOLLOW_LINKS)) {
            try (var walk = Files.walk(path)) {
                for (Path p : walk.sorted(Comparator.reverseOrder()).toList()) {
                    Files.delete(p);
                }
            }
        }
    }

    // Modules that jdeps finds in the helper JAR and the libraries of its Class-Path; each must be
    // pinned, so the runtime runs every class the helper can load. D8 runs only where javac
    // compiles guest Java, on a JDK, so lib/d8.jar is left out, and so are the Error Prone
    // annotations, whose IncompatibleModifiers and RequiredModifiers name java.compiler types but
    // have class retention, so no running helper loads them.
    static Set<String> helperModules(Path jar) throws Exception {
        List<String> libraries = new ArrayList<>();

        try (var file = new java.util.jar.JarFile(jar.toFile())) {
            String classPath = file.getManifest().getMainAttributes().getValue("Class-Path");

            for (String entry : classPath == null ? new String[0] : classPath.split(" ")) {
                Path library = jar.resolveSibling(entry);

                if (!entry.isBlank()
                        && !entry.equals("lib/d8.jar")
                        && !entry.startsWith("lib/error_prone_annotations-")
                        && Files.isRegularFile(library)) {
                    libraries.add(library.toString());
                }
            }
        }

        ToolProvider jdeps =
                ToolProvider.findFirst("jdeps")
                        .orElseThrow(() -> new IllegalStateException("jdeps"));
        StringWriter out = new StringWriter();
        List<String> arguments =
                new ArrayList<>(
                        List.of(
                                "--multi-release",
                                "25",
                                "--ignore-missing-deps",
                                "--print-module-deps"));

        if (!libraries.isEmpty()) {
            arguments.add("--class-path");
            arguments.add(String.join(java.io.File.pathSeparator, libraries));
        }

        arguments.add(jar.toString());
        need(
                jdeps.run(
                                new PrintWriter(out),
                                new PrintWriter(out),
                                arguments.toArray(String[]::new))
                        == 0,
                "jdeps failed: " + out);
        List<String> lines = out.toString().strip().lines().toList();

        return new TreeSet<>(List.of(lines.get(lines.size() - 1).strip().split(",")));
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 3 && args[0].equals("modules")) {
            Set<String> found = helperModules(Path.of(args[2]).toAbsolutePath());
            List<String> pinned = modules(pins(Path.of(args[1]).toAbsolutePath()));
            System.out.println(String.join(",", found));
            need(pinned.containsAll(found), "helper needs modules outside javaRuntime.modules");

            return;
        }

        need(
                args.length == 3,
                "ROOT DOWNLOADS OUTPUT, or modules ROOT HELPER_JAR for the jdeps check");
        Runtime.Version version = Runtime.version();
        Path root = Path.of(args[0]).toAbsolutePath().normalize(),
                downloads = Path.of(args[1]).toAbsolutePath().normalize(),
                output = Path.of(args[2]).toAbsolutePath().normalize();
        Map<String, Object> pins = pins(root);
        need(
                (version.feature()
                                + "."
                                + version.interim()
                                + "."
                                + version.update()
                                + "+"
                                + version.build().orElse(0))
                        .equals(Json.str(pins, "version")),
                "the pinned JDK " + Json.str(pins, "version") + " links the runtime");
        need(!Files.exists(output, LinkOption.NOFOLLOW_LINKS), "fresh output required");
        Path work = output.resolveSibling("." + output.getFileName() + ".work");
        remove(work);
        Files.createDirectories(work);
        Files.createDirectories(output);

        for (String host : HOSTS) {
            Path image = build(pins, host, downloads, work);
            Path archive = output.resolve("java-runtime-" + host + ".zip");
            zip(image, archive);
            System.out.println(sha(archive) + "  " + archive.getFileName());
        }

        remove(work);
    }
}
