package com.voyemu.prepare;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Flow;
import java.util.concurrent.TimeUnit;
import java.util.jar.Attributes;
import java.util.jar.JarEntry;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import javax.tools.ToolProvider;

public final class JavaHelperBuild {
    private static final List<String> AUDIO_SOURCES =
            List.of(
                    "src/guest/android/oem/audio/OemAudioPolicy.java",
                    "src/guest/android/oem/audio/OemAudioPolicyUnavailable.java");

    private static void need(boolean value, String message) {
        if (!value) {
            throw new IllegalArgumentException("JAVA_HELPER_BUILD: " + message);
        }
    }

    private static String sha(Path path) throws Exception {
        need(
                Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
                        && !Files.isSymbolicLink(path)
                        && Files.size(path) <= 256L * 1024 * 1024,
                "ordinary bounded input required: " + path);

        try (InputStream in = Files.newInputStream(path)) {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[65536];

            for (int n; (n = in.read(buffer)) >= 0; ) {
                if (n > 0) {
                    digest.update(buffer, 0, n);
                }
            }

            return HexFormat.of().formatHex(digest.digest());
        }
    }

    private static Path ordinary(Path path) throws Exception {
        Path p = path.toAbsolutePath().normalize();

        for (Path at = p; at != null; at = at.getParent()) {
            need(!Files.isSymbolicLink(at), "linked input: " + at);
        }

        return p;
    }

    private static Path dependency(
            Path cache, Path sdk, Path javaSdk, String filename, String expected, boolean sdkTool)
            throws Exception {
        need(
                expected.matches("[a-f0-9]{64}") && filename.matches("[A-Za-z0-9_.-]+\\.jar"),
                "dependency pin/name");
        List<Path> candidates = new ArrayList<>();
        candidates.add(cache.resolve(filename));
        candidates.add(cache.resolve(expected));
        candidates.add(cache.resolve(expected + ".blob"));

        if (sdkTool) {
            candidates.add(javaSdk.resolve("build-tools/36.1.0/lib").resolve(filename));
            candidates.add(sdk.resolve("build-tools/36.1.0/lib").resolve(filename));
        }

        for (Path candidate : candidates) {
            if (Files.exists(candidate, LinkOption.NOFOLLOW_LINKS)) {
                candidate = ordinary(candidate);
                need(sha(candidate).equals(expected), "dependency SHA mismatch: " + filename);

                return candidate;
            }
        }

        throw new FileNotFoundException(
                "Pinned dependency missing: "
                        + filename
                        + " SHA256="
                        + expected
                        + "; populate --cache or --sdk using project deps (no downloads"
                        + " performed)");
    }

    private static void ownedDestination(Path root, Path path) throws Exception {
        ordinary(path);
        need(
                path.startsWith(root.resolve(".local")) && !path.equals(root.resolve(".local")),
                "fetch destinations must stay inside project .local");
    }

    private static Path lockedPublish(
            Path root, Path target, String expected, long maximum, long exact, InputStream input)
            throws Exception {
        ownedDestination(root, target);
        need(
                expected.matches("[a-f0-9]{64}") && maximum > 0 && maximum <= 256L * 1024 * 1024,
                "publication pin/bound");
        Files.createDirectories(target.getParent());

        if (Files.exists(target, LinkOption.NOFOLLOW_LINKS)) {
            need(
                    sha(ordinary(target)).equals(expected),
                    "existing cache entry differs: " + target.getFileName());

            if (exact >= 0) {
                need(Files.size(target) == exact, "existing cache entry size");
            }

            input.close();

            return target;
        }

        Path lock = target.resolveSibling(target.getFileName() + ".lock"),
                temporary =
                        target.resolveSibling(
                                "." + target.getFileName() + ".part-" + UUID.randomUUID());
        Files.createDirectory(lock);

        try (input) {
            need(
                    Files.getFileStore(target.getParent()).getUsableSpace()
                            >= maximum + 64L * 1024 * 1024,
                    "disk reserve for bounded dependency");

            try (OutputStream out =
                    Files.newOutputStream(temporary, StandardOpenOption.CREATE_NEW)) {
                byte[] buffer = new byte[65536];
                long size = 0;

                for (int count; (count = input.read(buffer)) >= 0; ) {
                    if (count == 0) {
                        continue;
                    }

                    size += count;
                    need(
                            size <= maximum && (exact < 0 || size <= exact),
                            "entry exceeded declared extent");
                    out.write(buffer, 0, count);
                }

                need(exact < 0 || size == exact, "entry size differs");
            }

            need(sha(temporary).equals(expected), "entry SHA256 differs");
            need(
                    !Files.exists(target, LinkOption.NOFOLLOW_LINKS),
                    "cache destination appeared during publication");
            Files.move(temporary, target, StandardCopyOption.ATOMIC_MOVE);
            need(sha(target).equals(expected), "published entry SHA256 differs");

            return target;
        } finally {
            Files.deleteIfExists(temporary);
            Files.delete(lock);
        }
    }

    private static final class DownloadBody implements HttpResponse.BodySubscriber<Path> {
        private final CompletableFuture<Path> complete = new CompletableFuture<>();
        private final OutputStream output;
        private final Path file;
        private final long maximum, exact;
        private long size;
        private Flow.Subscription subscription;
        private boolean closed;

        DownloadBody(Path file, long maximum, long exact) throws Exception {
            this.file = file;
            this.maximum = maximum;
            this.exact = exact;
            output = Files.newOutputStream(file, StandardOpenOption.CREATE_NEW);
        }

        @Override
        public CompletionStage<Path> getBody() {
            return complete;
        }

        @Override
        public synchronized void onSubscribe(Flow.Subscription value) {
            if (closed || subscription != null) {
                value.cancel();

                return;
            }

            subscription = value;
            value.request(1);
        }

        @Override
        public synchronized void onNext(List<ByteBuffer> buffers) {
            if (closed) {
                return;
            }

            try {
                byte[] bytes = new byte[65536];

                for (ByteBuffer buffer : buffers) {
                    size += buffer.remaining();
                    need(
                            size <= maximum && (exact < 0 || size <= exact),
                            "download exceeded declared extent");

                    while (buffer.hasRemaining()) {
                        int count = Math.min(buffer.remaining(), bytes.length);
                        buffer.get(bytes, 0, count);
                        output.write(bytes, 0, count);
                    }
                }

                subscription.request(1);
            } catch (Throwable error) {
                // checkstyle-allow IllegalCatch: Flow.Subscriber forwards every failure to abort
                abort(error);
            }
        }

        @Override
        public synchronized void onError(Throwable error) {
            abort(error);
        }

        @Override
        public synchronized void onComplete() {
            if (closed) {
                return;
            }

            try {
                need(exact < 0 || size == exact, "download size differs");
                output.close();
                closed = true;
                complete.complete(file);
            } catch (Throwable error) {
                // checkstyle-allow IllegalCatch: Flow.Subscriber forwards every failure to abort
                abort(error);
            }
        }

        synchronized void abort(Throwable error) {
            if (closed) {
                return;
            }

            closed = true;

            if (subscription != null) {
                subscription.cancel();
            }

            try {
                output.close();
            } catch (IOException ignored) {
                // The original download failure is reported; a close failure adds nothing.
            }

            complete.completeExceptionally(error);
        }
    }

    private static final class HttpTransport {
        static final HttpClient CLIENT =
                HttpClient.newBuilder()
                        .connectTimeout(Duration.ofSeconds(20))
                        .followRedirects(HttpClient.Redirect.NORMAL)
                        .build();
    }

    private static Path fetch(
            Path root, Path target, String url, String expected, long maximum, long exact)
            throws Exception {
        ownedDestination(root, target);
        need(
                expected.matches("[a-f0-9]{64}") && maximum > 0 && maximum <= 256L * 1024 * 1024,
                "download pin/bound");

        if (Files.exists(target, LinkOption.NOFOLLOW_LINKS)) {
            need(
                    sha(ordinary(target)).equals(expected),
                    "existing dependency differs: " + target.getFileName());

            if (exact >= 0) {
                need(Files.size(target) == exact, "existing dependency size");
            }

            return target;
        }

        URI uri = URI.create(url);
        need(
                "https".equals(uri.getScheme())
                        && uri.getUserInfo() == null
                        && (uri.getPort() == -1 || uri.getPort() == 443),
                "pinned HTTPS URL required");
        Files.createDirectories(target.getParent());
        Path lock = target.resolveSibling(target.getFileName() + ".lock"),
                temporary =
                        target.resolveSibling(
                                "." + target.getFileName() + ".download-" + UUID.randomUUID());
        Files.createDirectory(lock);
        DownloadBody body = null;
        CompletableFuture<HttpResponse<Path>> pending = null;

        try {
            need(
                    Files.getFileStore(target.getParent()).getUsableSpace()
                            >= maximum + 64L * 1024 * 1024,
                    "disk reserve for bounded download");
            System.err.println(
                    "Fetching pinned " + target.getFileName() + " from " + uri.getHost());
            body = new DownloadBody(temporary, maximum, exact);
            final DownloadBody subscriber = body;
            HttpClient client = HttpTransport.CLIENT;
            HttpRequest request =
                    HttpRequest.newBuilder(uri).timeout(Duration.ofMinutes(3)).GET().build();
            pending =
                    client.sendAsync(
                            request,
                            info -> {
                                need(
                                        info.statusCode() == 200,
                                        "download HTTP status " + info.statusCode());
                                long declared =
                                        info.headers()
                                                .firstValueAsLong("content-length")
                                                .orElse(-1);
                                need(
                                        declared < 0
                                                || (declared <= maximum
                                                        && (exact < 0 || declared == exact)),
                                        "download Content-Length differs");

                                return subscriber;
                            });
            HttpResponse<Path> response = pending.get(190, TimeUnit.SECONDS);
            need("https".equals(response.uri().getScheme()), "download redirected away from HTTPS");
            need(sha(temporary).equals(expected), "download SHA256 differs");
            need(
                    !Files.exists(target, LinkOption.NOFOLLOW_LINKS),
                    "cache destination appeared during download");
            Files.move(temporary, target, StandardCopyOption.ATOMIC_MOVE);

            return target;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: rollback, then rethrow unchanged
            if (body != null) {
                body.abort(error);
            }

            if (pending != null) {
                pending.cancel(true);
            }

            throw error;
        } finally {
            Files.deleteIfExists(temporary);
            Files.delete(lock);
        }
    }

    private static Path archive(Path root, Path archives, Map<String, Object> spec)
            throws Exception {
        String expected = Json.str(spec, "sha256"), url = Json.str(spec, "url");

        need(expected.matches("[a-f0-9]{64}"), "SDK archive pin");
        List<Path> candidates =
                List.of(archives.resolve(expected + ".zip"), archives.resolve(expected + ".blob"));

        for (Path p : candidates) {
            if (Files.exists(p, LinkOption.NOFOLLOW_LINKS)) {
                p = ordinary(p);
                need(sha(p).equals(expected), "cached SDK archive differs from pin");

                return p;
            }
        }

        return fetch(
                root, archives.resolve(expected + ".zip"), url, expected, 256L * 1024 * 1024, -1);
    }

    /** SDK archive entry selected by its full name or by its path suffix. */
    private record SdkEntry(String suffix, boolean exactName) {}

    private static Path extractJar(
            Path root,
            Path archive,
            String archivePin,
            Path target,
            SdkEntry wanted,
            String expected,
            long exact)
            throws Exception {
        String suffix = wanted.suffix();
        boolean exactName = wanted.exactName();

        need(sha(ordinary(archive)).equals(archivePin), "SDK archive changed before extraction");

        try (ZipFile zip = new ZipFile(archive.toFile())) {
            ZipEntry selected = null;
            Set<String> seen = new HashSet<>();
            int count = 0;

            for (var entries = zip.entries(); entries.hasMoreElements(); ) {
                ZipEntry entry = entries.nextElement();
                need(++count <= 200000, "SDK archive entry count");
                String name = entry.getName();
                need(seen.add(name), "duplicate SDK ZIP entry");
                boolean match =
                        exactName
                                ? name.equals(suffix)
                                : name.equals(suffix) || name.endsWith("/" + suffix);

                if (!match) {
                    continue;
                }

                need(
                        selected == null
                                && !entry.isDirectory()
                                && !name.startsWith("/")
                                && !name.contains("\\")
                                && !name.contains(":"),
                        "unique confined SDK JAR entry");

                for (String part : name.split("/", -1)) {
                    need(!part.equals("..") && !part.equals("."), "SDK JAR traversal");
                }

                need(
                        entry.getSize() > 0 && entry.getSize() <= 128L * 1024 * 1024,
                        "SDK JAR extent");
                selected = entry;
            }

            need(selected != null, "SDK JAR entry missing: " + suffix);

            if (exact >= 0) {
                need(selected.getSize() == exact, "SDK JAR expected extent");
            }

            Path result =
                    lockedPublish(
                            root,
                            target,
                            expected,
                            128L * 1024 * 1024,
                            selected.getSize(),
                            zip.getInputStream(selected));
            need(sha(archive).equals(archivePin), "SDK archive changed during extraction");

            return result;
        }
    }

    private static Map<String, Object> prepareJavaSdk(
            Path root, Path sdk, Path javaSdk, Path archives, Map<String, Object> toolPins)
            throws Exception {
        ownedDestination(root, javaSdk);
        ownedDestination(root, archives);
        Map<String, Object>
                manifest =
                        Json.object(
                                Json.parse(
                                        Files.readString(root.resolve("src/app/toolchain.json")))),
                sdkSpec = Json.obj(manifest, "sdk");
        need(
                Json.str(sdkSpec, "buildTools").equals("36.1.0")
                        && Json.integer(sdkSpec, "compileSdk") == 34,
                "registered Java SDK34 recipe required");
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT),
                host =
                        os.contains("mac") || os.contains("darwin")
                                ? "darwin"
                                : os.contains("windows")
                                        ? "win32"
                                        : os.contains("linux") ? "linux" : "";
        need(!host.isEmpty(), "Java SDK fetch supports macOS, Windows and Linux");
        Map<String, Object> pins = Json.obj(Json.obj(sdkSpec, "archives"), host),
                buildSpec = Json.obj(pins, "buildTools"),
                platformSpec = Json.obj(pins, "platform");
        Map<String, Object> files = new TreeMap<>();
        Path buildArchive = null;

        for (String name : List.of("d8", "apksigner")) {
            String filename = name + ".jar",
                    expected = Json.str(Json.obj(toolPins, name), "sha256");
            Path target = javaSdk.resolve("build-tools/36.1.0/lib/" + filename);

            if (Files.exists(target, LinkOption.NOFOLLOW_LINKS)) {
                need(sha(ordinary(target)).equals(expected), "existing Java SDK JAR differs");
            } else {
                Path full = sdk.resolve("build-tools/36.1.0/lib/" + filename);

                if (Files.exists(full, LinkOption.NOFOLLOW_LINKS)) {
                    need(sha(ordinary(full)).equals(expected), "full SDK JAR differs");
                    lockedPublish(
                            root,
                            target,
                            expected,
                            128L * 1024 * 1024,
                            Files.size(full),
                            Files.newInputStream(full));
                } else {
                    if (buildArchive == null) {
                        buildArchive = archive(root, archives, buildSpec);
                    }

                    extractJar(
                            root,
                            buildArchive,
                            Json.str(buildSpec, "sha256"),
                            target,
                            new SdkEntry("lib/" + filename, false),
                            expected,
                            -1);
                }
            }

            files.put(
                    javaSdk.relativize(target).toString().replace('\\', '/'),
                    Map.of("sha256", expected, "bytes", Files.size(target)));
        }

        // The platform34 archive entry pin is sdk.platformJar, never caller bytes.
        Map<String, Object> platformJar = Json.obj(sdkSpec, "platformJar");
        String platformPin = Json.str(platformJar, "sha256");
        long platformBytes = Json.integer(platformJar, "bytes");
        Path platform = javaSdk.resolve("platforms/android-34/android.jar");

        if (Files.exists(platform, LinkOption.NOFOLLOW_LINKS)) {
            need(
                    sha(ordinary(platform)).equals(platformPin)
                            && Files.size(platform) == platformBytes,
                    "existing public platform JAR differs");
        } else {
            Path platformArchive = archive(root, archives, platformSpec);
            extractJar(
                    root,
                    platformArchive,
                    Json.str(platformSpec, "sha256"),
                    platform,
                    new SdkEntry("android-34/android.jar", true),
                    platformPin,
                    platformBytes);
        }

        files.put(
                "platforms/android-34/android.jar",
                Map.of("sha256", platformPin, "bytes", Files.size(platform)));

        return Map.of(
                "root",
                javaSdk.toString(),
                "files",
                files,
                "build_tools_archive_sha256",
                Json.str(buildSpec, "sha256"),
                "platform_archive_sha256",
                Json.str(platformSpec, "sha256"),
                "native_sdk_modified",
                false);
    }

    private static Map<String, String> sourcePins(Path root) throws Exception {
        Map<String, String> pins = new TreeMap<>();
        Path source = root.resolve("src/prepare/java");

        try (var walk = Files.walk(source)) {
            for (Path p :
                    walk.filter(f -> !Files.isDirectory(f, LinkOption.NOFOLLOW_LINKS))
                            .sorted()
                            .toList()) {
                ordinary(p);
                need(p.toString().endsWith(".java"), "unexpected Java source entry");
                need(pins.size() < 256, "source count");
                pins.put(root.relativize(p).toString().replace('\\', '/'), sha(p));
            }
        }

        Path fixture =
                root.resolve(
                        "src/prepare/test/fixtures/com/voyemu/prepare/fixture/BuildMarker.java");
        ordinary(fixture);
        pins.put(root.relativize(fixture).toString().replace('\\', '/'), sha(fixture));

        return pins;
    }

    private static void compile(Path classes, List<Path> libs, List<Path> sources, String release)
            throws Exception {
        var compiler = ToolProvider.getSystemJavaCompiler();

        need(compiler != null, "a JDK with javac is required");
        List<String> options =
                new ArrayList<>(
                        List.of(
                                "--release",
                                release,
                                "-encoding",
                                "UTF-8",
                                "-proc:none",
                                "-d",
                                classes.toString()));

        if (!libs.isEmpty()) {
            options.add("-classpath");
            options.add(
                    String.join(File.pathSeparator, libs.stream().map(Path::toString).toList()));
        }

        for (Path source : sources) {
            options.add(source.toString());
        }

        need(
                compiler.run(null, System.out, System.err, options.toArray(String[]::new)) == 0,
                "javac failed");
    }

    private static void jarEntry(JarOutputStream out, String name, byte[] bytes) throws Exception {
        need(
                !name.startsWith("/") && !name.contains("\\") && !name.contains(".."),
                "unsafe JAR entry");
        JarEntry entry = new JarEntry(name);
        entry.setTime(0);
        out.putNextEntry(entry);
        out.write(bytes);
        out.closeEntry();
    }

    private static void removeOwned(Path root) throws Exception {
        if (!Files.exists(root, LinkOption.NOFOLLOW_LINKS)) {
            return;
        }

        try (var walk = Files.walk(root)) {
            for (Path p : walk.sorted(Comparator.reverseOrder()).toList()) {
                Files.delete(p);
            }
        }
    }

    private record Dep(String name, String filename, String sha, Path path) {}

    // The java-helper asset: the helper of ROOT built offline from LIBRARIES and the SDK JARs of
    // ROOT/.local/toolchains/java-sdk, written as OUTPUT/java-helper.zip with sorted entries and
    // the earliest ZIP time. d8.jar and apksigner.jar stay out; preparation adds them from its
    // own Java SDK.
    private static void asset(Path root, Path libraries, Path output) throws Exception {
        need(!Files.exists(output, LinkOption.NOFOLLOW_LINKS), "fresh asset output required");
        Path helper = output.resolve("helper");
        Files.createDirectories(output);
        build(
                new String[] {
                    root.toString(),
                    libraries.toString(),
                    root.resolve(".local/toolchains/java-sdk").toString(),
                    helper.toString(),
                    "-",
                    "offline",
                    root.resolve(".local/toolchains/downloads/sdk").toString()
                });
        Map<String, Path> entries = new TreeMap<>();

        try (var walk = Files.walk(helper)) {
            for (Path p : walk.filter(Files::isRegularFile).toList()) {
                String name = helper.relativize(p).toString().replace('\\', '/');

                if (!name.equals("lib/d8.jar") && !name.equals("lib/apksigner.jar")) {
                    entries.put(name, p);
                }
            }
        }

        try (var out =
                new java.util.zip.ZipOutputStream(
                        Files.newOutputStream(
                                output.resolve("java-helper.zip"),
                                StandardOpenOption.CREATE_NEW))) {
            for (var e : entries.entrySet()) {
                ZipEntry entry = new ZipEntry(e.getKey());
                entry.setTimeLocal(java.time.LocalDateTime.of(1980, 1, 1, 0, 0));
                out.putNextEntry(entry);
                out.write(Files.readAllBytes(e.getValue()));
                out.closeEntry();
            }
        }

        removeOwned(helper);
        System.out.println(sha(output.resolve("java-helper.zip")) + "  java-helper.zip");
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 4 && args[0].equals("asset")) {
            asset(
                    ordinary(Path.of(args[1])),
                    ordinary(Path.of(args[2])),
                    ordinary(Path.of(args[3])));

            return;
        }

        build(args);
    }

    private static void build(String[] args) throws Exception {
        need(
                args.length == 7 || args.length == 8,
                "root cache sdk destination resource-snapshot-or-dash offline-or-fetch"
                        + " archive-cache [source-snapshot], or asset root libraries output");
        need(Runtime.version().feature() >= 25, "JDK25 or newer required");
        Path root = ordinary(Path.of(args[0])),
                cache = ordinary(Path.of(args[1])),
                sdk = ordinary(Path.of(args[2])),
                output = ordinary(Path.of(args[3])),
                javaSdk = root.resolve(".local/toolchains/java-sdk"),
                archives = ordinary(Path.of(args[6]));
        need(
                args[5].equals("offline") || args[5].equals("fetch"),
                "explicit offline/fetch mode required");
        boolean online = args[5].equals("fetch");

        if (online) {
            ownedDestination(root, cache);
            ownedDestination(root, archives);
        }

        need(
                output.startsWith(root)
                        && !output.equals(root)
                        && !Files.exists(output, LinkOption.NOFOLLOW_LINKS),
                "fresh output inside project required");
        Path sourceRoot = args.length == 8 ? ordinary(Path.of(args[7])) : root;

        if (args.length == 8) {
            need(
                    sourceRoot.startsWith(root.resolve(".local/build/tmp"))
                            && !sourceRoot.equals(root.resolve(".local/build/tmp")),
                    "owned source snapshot required");
        }

        Path librariesSpec = root.resolve("src/prepare/libraries.json"),
                toolsSpec = root.resolve("src/prepare/toolchain.json");
        String librarySpecPin = sha(librariesSpec),
                toolSpecPin = sha(toolsSpec),
                sdkManifestPin = sha(root.resolve("src/app/toolchain.json"));
        Map<String, String> sources = sourcePins(sourceRoot);
        need(!sources.isEmpty(), "Java sources missing");
        Map<String, Object> toolPins =
                Json.obj(Json.object(Json.parse(Files.readString(toolsSpec))), "libraries");
        Map<String, Object> javaSdkInfo =
                online ? prepareJavaSdk(root, sdk, javaSdk, archives, toolPins) : Map.of();
        List<Dep> dependencies =
                dependencies(root, cache, sdk, javaSdk, librariesSpec, toolPins, online);
        Files.createDirectories(output.getParent());
        Path stage =
                output.getParent()
                        .resolve("." + output.getFileName() + ".pending-" + UUID.randomUUID());
        Files.createDirectory(stage);
        boolean published = false;

        try {
            Path classes = Files.createDirectory(stage.resolve("classes")),
                    lib = Files.createDirectory(stage.resolve("lib"));
            List<Path> classpath = new ArrayList<>();
            List<Map<String, Object>> depReceipt = new ArrayList<>();

            for (Dep d : dependencies) {
                Path target = lib.resolve(d.filename());
                Files.copy(d.path(), target);
                need(sha(target).equals(d.sha()), "copied dependency differs");
                classpath.add(target);
                depReceipt.add(
                        new TreeMap<>(
                                Map.of(
                                        "name",
                                        d.name(),
                                        "file",
                                        "lib/" + d.filename(),
                                        "sha256",
                                        d.sha(),
                                        "bytes",
                                        Files.size(target))));
            }

            List<Path> main = new ArrayList<>();

            for (String relative : sources.keySet()) {
                if (relative.startsWith("src/prepare/java/")) {
                    main.add(sourceRoot.resolve(relative));
                }
            }

            compile(classes, classpath, main, "25");
            compile(
                    classes,
                    List.of(),
                    List.of(
                            sourceRoot.resolve(
                                    "src/prepare/test/fixtures/com/voyemu/prepare/fixture/BuildMarker.java")),
                    "8");
            Map<String, byte[]> resources = new TreeMap<>();
            Map<String, String> resourcePins = new TreeMap<>();
            Path resourceRoot = null;

            if (!args[4].equals("-")) {
                resourceRoot = ordinary(Path.of(args[4]));
                snapshotResources(resourceRoot, resources, resourcePins);
            }

            embedAudioSources(root, resourceRoot, resources, resourcePins);
            Path jar = writeJar(stage, classes, depReceipt, resources);
            need(
                    sourcePins(sourceRoot).equals(sources)
                            && sha(librariesSpec).equals(librarySpecPin)
                            && sha(toolsSpec).equals(toolSpecPin)
                            && sha(root.resolve("src/app/toolchain.json")).equals(sdkManifestPin),
                    "source/metadata changed during compilation");

            for (Dep d : dependencies) {
                need(sha(d.path()).equals(d.sha()), "dependency changed during compilation");
            }

            verifySnapshotsUnchanged(root, resourceRoot, resourcePins);
            Map<String, Object> receipt = new LinkedHashMap<>();
            receipt.put("schema", 1);
            receipt.put("kind", "java-preparation-helper");
            receipt.put("java_release", 25);
            receipt.put("build_jdk", System.getProperty("java.version"));
            receipt.put("entrypoint", "com.voyemu.prepare.PreparationMain");
            receipt.put("jar", "voyemu-java-helper.jar");
            receipt.put("sha256", sha(jar));
            receipt.put("bytes", Files.size(jar));
            receipt.put("fetch_enabled", online);
            receipt.put("java_sdk", javaSdkInfo);
            receipt.put("dependencies", depReceipt);
            receipt.put(
                    "embedded_profiles",
                    resourceRoot != null && resources.containsKey("voyemu/profile-index.json"));
            receipt.put("node_required", false);
            receipt.put("python_required", false);
            receipt.put("java_required", true);
            receipt.put("graal_required", false);
            receipt.put("scope", "source preparation helper invoked by the native Voyemu CLI");
            Files.writeString(
                    stage.resolve("build-receipt.json"),
                    Json.write(receipt) + "\n",
                    StandardOpenOption.CREATE_NEW);
            removeOwned(classes);
            Files.move(stage, output, StandardCopyOption.ATOMIC_MOVE);
            published = true;
            System.out.println(Json.write(receipt));
        } finally {
            if (!published) {
                removeOwned(stage);
            }
        }
    }

    private static List<Dep> dependencies(
            Path root,
            Path cache,
            Path sdk,
            Path javaSdk,
            Path librariesSpec,
            Map<String, Object> toolPins,
            boolean online)
            throws Exception {
        List<Dep> dependencies = new ArrayList<>();
        Set<String> names = new HashSet<>();

        for (Object item : Json.list(Json.parse(Files.readString(librariesSpec)))) {
            Map<String, Object> d = Json.object(item);
            String filename = Json.str(d, "filename"), expected = Json.str(d, "sha256");
            need(names.add(filename), "duplicate dependency filename");
            Path selected;

            try {
                selected = dependency(cache, sdk, javaSdk, filename, expected, false);
            } catch (FileNotFoundException absent) {
                if (!online) {
                    throw absent;
                }

                long exact = d.containsKey("bytes") ? Json.integer(d, "bytes") : -1;
                need(exact < 0 || exact <= 32L * 1024 * 1024, "Maven JAR expected extent");
                selected =
                        fetch(
                                root,
                                cache.resolve(filename),
                                Json.str(d, "url"),
                                expected,
                                32L * 1024 * 1024,
                                exact);
            }

            dependencies.add(new Dep(Json.str(d, "name"), filename, expected, selected));
        }

        for (String name : List.of("d8", "apksigner")) {
            Map<String, Object> d = Json.obj(toolPins, name);
            String filename = name + ".jar", expected = Json.str(d, "sha256");
            need(names.add(filename), "duplicate SDK dependency");
            dependencies.add(
                    new Dep(
                            name,
                            filename,
                            expected,
                            dependency(cache, sdk, javaSdk, filename, expected, true)));
        }

        need(dependencies.size() <= 64, "dependency count");

        return dependencies;
    }

    private static void snapshotResources(
            Path resourceRoot, Map<String, byte[]> resources, Map<String, String> resourcePins)
            throws Exception {
        need(
                Files.isRegularFile(resourceRoot.resolve("voyemu/profile-index.json")),
                "explicit generated resource snapshot missing profile-index.json");
        long total = 0;

        try (var walk = Files.walk(resourceRoot)) {
            for (Path p :
                    walk.filter(f -> !Files.isDirectory(f, LinkOption.NOFOLLOW_LINKS))
                            .sorted()
                            .toList()) {
                ordinary(p);
                need(
                        Files.size(p) <= 32L * 1024 * 1024 && resourcePins.size() < 4096,
                        "resource snapshot bounds");
                String relative = resourceRoot.relativize(p).toString().replace('\\', '/');
                need(
                        relative.startsWith("voyemu/")
                                && !relative.endsWith(".apk")
                                && !relative.endsWith(".img"),
                        "only public voyemu resources allowed");
                byte[] bytes = Files.readAllBytes(p);
                total += bytes.length;
                need(total <= 64L * 1024 * 1024, "resource snapshot byte budget");
                String bytesPin =
                        HexFormat.of()
                                .formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
                need(sha(p).equals(bytesPin), "resource changed during read");
                resources.put(relative, bytes);
                resourcePins.put(relative, bytesPin);
            }
        }
    }

    private static void embedAudioSources(
            Path root,
            Path resourceRoot,
            Map<String, byte[]> resources,
            Map<String, String> resourcePins)
            throws Exception {
        for (String path : AUDIO_SOURCES) {
            Path source = ordinary(root.resolve(path));
            need(Files.size(source) <= 1024 * 1024, "AudioPolicy source exceeds bound");
            String name = "voyemu/" + path;
            byte[] value = Files.readAllBytes(source);
            String pin =
                    HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value));

            if (resourceRoot != null) {
                need(
                        resourcePins.containsKey(name) && resourcePins.get(name).equals(pin),
                        "AudioPolicy snapshot missing or differs");
            }

            resources.put(name, value);
            resourcePins.put(name, pin);
        }

        for (String name : resourcePins.keySet()) {
            if (name.startsWith("voyemu/src/guest/android/oem/audio/")) {
                need(
                        AUDIO_SOURCES.contains(name.substring("voyemu/".length())),
                        "Unlisted audio source resource");
            }
        }
    }

    private static Path writeJar(
            Path stage,
            Path classes,
            List<Map<String, Object>> depReceipt,
            Map<String, byte[]> resources)
            throws Exception {
        Manifest manifest = new Manifest();
        Attributes attrs = manifest.getMainAttributes();

        attrs.putValue("Manifest-Version", "1.0");
        attrs.putValue("Main-Class", "com.voyemu.prepare.PreparationMain");
        attrs.putValue(
                "Class-Path",
                String.join(" ", depReceipt.stream().map(d -> (String) d.get("file")).toList()));
        ByteArrayOutputStream manifestBytes = new ByteArrayOutputStream();
        manifest.write(manifestBytes);
        Path jar = stage.resolve("voyemu-java-helper.jar");

        try (JarOutputStream out =
                new JarOutputStream(Files.newOutputStream(jar, StandardOpenOption.CREATE_NEW))) {
            jarEntry(out, "META-INF/MANIFEST.MF", manifestBytes.toByteArray());

            try (var walk = Files.walk(classes)) {
                for (Path p : walk.filter(Files::isRegularFile).sorted().toList()) {
                    jarEntry(
                            out,
                            classes.relativize(p).toString().replace('\\', '/'),
                            Files.readAllBytes(p));
                }
            }

            for (var e : resources.entrySet()) {
                jarEntry(out, e.getKey(), e.getValue());
            }
        }

        return jar;
    }

    private static void verifySnapshotsUnchanged(
            Path root, Path resourceRoot, Map<String, String> resourcePins) throws Exception {
        if (resourceRoot != null) {
            for (var e : resourcePins.entrySet()) {
                need(
                        sha(resourceRoot.resolve(e.getKey())).equals(e.getValue()),
                        "resource snapshot changed during build");
            }
        }

        for (String path : AUDIO_SOURCES) {
            need(
                    sha(ordinary(root.resolve(path))).equals(resourcePins.get("voyemu/" + path)),
                    "AudioPolicy source changed during build");
        }
    }
}
