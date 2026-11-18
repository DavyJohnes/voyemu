package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.UUID;

/** Owner-requested acquisition only: adb reads/pull and read-only qconn file service. */
public final class VehicleCaptureMain {
    static void require(boolean value, String message) {
        Failure.require(value, "VEHICLE_CAPTURE", message, 3);
    }

    static String relative(String value) {
        require(
                value.matches("[A-Za-z0-9_./+=@~-]+")
                        && !value.startsWith("/")
                        && !value.endsWith("/"),
                "Ordinary relative destination required");

        for (String part : value.split("/", -1)) {
            require(
                    !part.isEmpty() && !part.equals(".") && !part.equals(".."),
                    "Destination traversal rejected");
        }

        return value;
    }

    static boolean remote(String value) {
        if (!value.matches(
                "/(?:system|system_ext|vendor|product|data|resc)/[A-Za-z0-9_./+=@~:-]+")) {
            return false;
        }

        for (String part : value.split("/", -1)) {
            if (part.equals(".") || part.equals("..")) {
                return false;
            }
        }

        return true;
    }

    static void validate(Map<String, Object> spec, String profile) {
        require(
                Json.integer(spec, "schema") == 1 && Json.str(spec, "profile").equals(profile),
                "Capture identity mismatch");
        List<Object> android = Json.list(spec.get("android"));
        require(
                !android.isEmpty() && android.size() <= 128,
                "Bounded Android input declarations required");
        HashSet<String> names = new HashSet<>(), paths = new HashSet<>();

        for (Object value : android) {
            Map<String, Object> item = Json.object(value), source = Json.obj(item, "source");
            String file = relative(Json.str(item, "file"));
            require(
                    names.add(Json.str(item, "name"))
                            && paths.add(file.toLowerCase(java.util.Locale.ROOT)),
                    "Duplicate acquisition input");

            if (source.containsKey("package")) {
                require(
                        source.size() == 1
                                && Json.str(source, "package")
                                        .matches(
                                                "[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+"),
                        "Explicit package required");
            } else {
                require(
                        source.size() == 1 && remote(Json.str(source, "path")),
                        "Explicit system source required");
            }
        }

        Map<String, Object> qnx = Json.obj(spec, "qnx");
        String host = Json.str(qnx, "host");
        require(
                host.matches("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}"),
                "Explicit IPv4 qconn endpoint required");

        for (String octet : host.split("\\.", -1)) {
            require(Integer.parseInt(octet) <= 255, "Invalid qconn address");
        }

        require(
                Json.integer(qnx, "port") > 0 && Json.integer(qnx, "port") <= 65535,
                "Bounded qconn port required");
        names.clear();
        List<Object> images = Json.list(qnx.get("images"));
        require(!images.isEmpty() && images.size() <= 8, "Bounded QNX roles required");

        for (Object value : images) {
            Map<String, Object> item = Json.object(value);
            String name = Json.str(item, "file");
            require(
                    name.matches("[A-Za-z0-9_-]+\\.img") && names.add(name),
                    "Unique QNX IMG role required");

            if (item.containsKey("vehiclePath")) {
                require(
                        Json.str(item, "vehiclePath").matches("/dev/disk/[A-Za-z0-9_.-]+"),
                        "Explicit read-only QNX partition required");
            } else {
                require(
                        Json.str(item, "partitionLabel").matches("[A-Za-z0-9_-]+"),
                        "Unresolved QNX role must name a partition label");
            }
        }
    }

    static List<String> members(String root, String listing) {
        List<String> result = new ArrayList<>();
        HashSet<String> names = new HashSet<>();

        for (String line : listing.lines().toList()) {
            require(line.startsWith(root + "/"), "Remote tree escaped declared source");
            String member = relative(line.substring(root.length() + 1));
            require(
                    names.add(member.toLowerCase(java.util.Locale.ROOT)),
                    "Duplicate remote member");
            result.add(member);
        }

        require(
                !result.isEmpty() && result.size() <= 8192,
                "Bounded nonempty remote tree required");
        result.sort(String::compareTo);

        return result;
    }

    static void space(Path root, long reserve) throws Exception {
        require(
                Files.getFileStore(root).getUsableSpace() >= reserve,
                "Disk reserve of " + reserve + " bytes required");
    }

    static void fresh(Path path, Object value) throws Exception {
        Files.writeString(
                path,
                Json.write(value) + "\n",
                StandardOpenOption.CREATE_NEW,
                StandardOpenOption.WRITE);
    }

    static void writePartition(
            QnxFileClient client, QnxFileClient.Open source, Path target, Path root, long reserve)
            throws Exception {
        try (FileChannel file =
                FileChannel.open(
                        target,
                        StandardOpenOption.CREATE_NEW,
                        StandardOpenOption.WRITE,
                        StandardOpenOption.SPARSE)) {
            long offset = 0;

            while (offset < source.bytes()) {
                int count = (int) Math.min(65536, source.bytes() - offset);
                byte[] bytes = client.read(source.fd(), offset, count);
                require(
                        bytes.length > 0 && bytes.length <= count,
                        "Partition read extent mismatch");
                boolean zero = true;

                for (byte value : bytes) {
                    if (value != 0) {
                        zero = false;
                        break;
                    }
                }

                if (!zero) {
                    file.position(offset);
                    ByteBuffer buffer = ByteBuffer.wrap(bytes);

                    while (buffer.hasRemaining()) {
                        file.write(buffer);
                    }
                }

                offset += bytes.length;

                if (offset % (64L << 20) == 0) {
                    space(root, reserve);
                }
            }

            if (file.size() < source.bytes()) {
                file.position(source.bytes() - 1);
                file.write(ByteBuffer.wrap(new byte[] {0}));
            }

            file.force(true);
            require(file.size() == source.bytes(), "Partition output extent mismatch");
        }
    }

    static void validateImport(Path source, Path destination) throws Exception {
        for (var entry : FilesSupport.tree(source).entrySet()) {
            Path target = Storage.confined(destination, entry.getKey());
            require(
                    !Files.exists(target)
                            || (Files.isRegularFile(target)
                                    && FilesSupport.hash(target)
                                            .equals(FilesSupport.hash(entry.getValue()))),
                    "Existing owner input differs; select a separate source profile before"
                            + " capture");
        }
    }

    static void publishImport(Path source, Path destination) throws Exception {
        Storage.directory(destination);

        for (var entry : FilesSupport.tree(source).entrySet()) {
            Path target = Storage.confined(destination, entry.getKey());
            Storage.directory(target.getParent());

            if (Files.exists(target)) {
                require(
                        Files.isRegularFile(target)
                                && FilesSupport.hash(target)
                                        .equals(FilesSupport.hash(entry.getValue())),
                        "Existing owner input changed during capture");
                Files.delete(entry.getValue());
            } else {
                Files.move(entry.getValue(), target);
            }
        }

        FilesSupport.removeOwnedTree(source);
    }

    static Map<String, Object> run(Map<String, Object> request) throws Exception {
        return run(request, 20L << 30);
    }

    /**
     * Runs a capture that keeps RESERVE bytes free; tests pass a reserve sized to their fixture.
     */
    static Map<String, Object> run(Map<String, Object> request, long reserve) throws Exception {
        require(Json.integer(request, "schema") == 1, "Schema1 required");
        String profile = Json.str(request, "profile"), serial = Json.str(request, "serial");
        require(
                profile.matches("[a-z0-9][a-z0-9._-]+")
                        && serial.matches("[A-Za-z0-9][A-Za-z0-9_.:-]*")
                        && !serial.startsWith("emulator-"),
                "Explicit physical vehicle serial required");
        Path root = Path.of(Json.str(request, "root"));
        require(
                root.isAbsolute() && root.toRealPath().equals(root),
                "Canonical project root required");
        Storage.confined(root, ".");
        Path directory = Storage.confined(root, "profiles/" + profile),
                capture = directory.resolve("capture.json"),
                draft = directory.resolve("profile.json");
        require(
                FilesSupport.hash(capture).equals(Json.str(request, "capture_sha256"))
                        && FilesSupport.hash(draft).equals(Json.str(request, "draft_sha256")),
                "Capture/draft inputs changed");
        Map<String, Object> spec = Json.object(Json.parse(Files.readString(capture)));
        validate(spec, profile);
        Path adb = Path.of(Json.str(request, "adb"));
        require(
                adb.isAbsolute() && FilesSupport.hash(adb).equals(Json.str(request, "adb_sha256")),
                "Pinned native adb required");
        Storage.confined(adb, ".");
        Path local = root.resolve(".local");
        Storage.directory(local.resolve("build/tmp"));
        Storage.directory(local.resolve("profiles"));
        space(root, reserve);
        String token = UUID.randomUUID().toString();
        Path lock = local.resolve("profiles/" + profile + ".capture.lock");
        Files.createDirectory(lock);
        fresh(lock.resolve("owner.json"), Map.of("token", token));
        Path stage = local.resolve("build/tmp/vehicle-capture-" + token);
        Files.createDirectory(stage);
        Map<String, Object> receipt = new LinkedHashMap<>();
        receipt.put("schema", 1);
        receipt.put("profile", profile);
        receipt.put("source", "vehicle");
        receipt.put("checked_at", Instant.now().toString());
        receipt.put("vehicle_modified", false);
        receipt.put("runtime_accepted", false);
        Map<String, Object> android = new TreeMap<>();
        List<Object> qnx = new ArrayList<>(), missing = new ArrayList<>();
        receipt.put("android", android);
        receipt.put("qnx", qnx);
        receipt.put("missing_qnx_roles", missing);

        try {
            var ready =
                    Processes.run(
                            adb,
                            List.of("-s", serial, "get-state"),
                            root,
                            Duration.ofSeconds(10),
                            false);
            require(
                    ready.stdout().strip().equals("device"),
                    "Selected physical vehicle is not ready");
            Path originals = stage.resolve("originals");
            Files.createDirectory(originals);
            captureAndroid(spec, adb, serial, root, originals, android, reserve);
            Path partitions = stage.resolve("qnx");
            Files.createDirectory(partitions);
            Map<String, Object> endpoint = Json.obj(spec, "qnx");

            for (Object value : Json.list(endpoint.get("images"))) {
                Map<String, Object> item = Json.object(value);

                if (!item.containsKey("vehiclePath")) {
                    retainedQnxImage(
                            Json.str(item, "file"), local, profile, partitions, qnx, missing);
                    continue;
                }

                qnx.add(vehicleQnxImage(item, endpoint, adb, serial, root, partitions, reserve));
            }

            require(
                    FilesSupport.hash(capture).equals(Json.str(request, "capture_sha256"))
                            && FilesSupport.hash(draft).equals(Json.str(request, "draft_sha256")),
                    "Capture/draft changed during acquisition");
            fresh(stage.resolve("import-receipt.json"), receipt);
            Path output = local.resolve("profiles/" + profile);
            Storage.directory(output);

            for (String group : List.of("originals", "qnx")) {
                validateImport(stage.resolve(group), output.resolve(group));
            }

            for (String group : List.of("originals", "qnx")) {
                publishImport(stage.resolve(group), output.resolve(group));
            }

            Path receiptFile = output.resolve("import-" + token + ".json");
            Files.move(stage.resolve("import-receipt.json"), receiptFile);
            updateDraftProfile(draft, directory, token, android);
            fresh(
                    output.resolve("capture-cluster-" + token + ".json"),
                    Map.of(
                            "schema",
                            1,
                            "profile",
                            profile,
                            "preferredSource",
                            "vehicle",
                            "images",
                            qnx,
                            "missing_qnx_roles",
                            missing));
            updateQnxProfile(directory, token, qnx);
            Files.delete(stage);

            return Map.of(
                    "schema",
                    1,
                    "profile",
                    profile,
                    "inputs",
                    output.toString(),
                    "receipt",
                    receiptFile.toString(),
                    "vehicle_modified",
                    false,
                    "runtime_accepted",
                    false,
                    "missing_qnx_roles",
                    missing);
        } catch (Throwable error) {
            // checkstyle-allow IllegalCatch: rollback, then rethrow unchanged
            fresh(stage.resolve("partial-receipt.json"), receipt);
            throw error;
        } finally {
            require(
                    Json.str(
                                    Json.object(
                                            Json.parse(
                                                    Files.readString(lock.resolve("owner.json")))),
                                    "token")
                            .equals(token),
                    "Capture lock owner changed");
            Files.deleteIfExists(lock.resolve("owner.json"));
            Files.deleteIfExists(lock);
        }
    }

    private static void captureAndroid(
            Map<String, Object> spec,
            Path adb,
            String serial,
            Path root,
            Path originals,
            Map<String, Object> android,
            long reserve)
            throws Exception {
        long total = 0;

        for (Object value : Json.list(spec.get("android"))) {
            Map<String, Object> item = Json.object(value), source = Json.obj(item, "source");
            String origin;

            if (source.containsKey("package")) {
                String resolved =
                        Processes.run(
                                        adb,
                                        List.of(
                                                "-s",
                                                serial,
                                                "shell",
                                                "pm",
                                                "path",
                                                Json.str(source, "package")),
                                        root,
                                        Duration.ofSeconds(15),
                                        false)
                                .stdout()
                                .strip();
                require(
                        resolved.startsWith("package:")
                                && !resolved.contains("\n")
                                && !resolved.contains("\r"),
                        "One installed base APK required");
                origin = resolved.substring(8);
            } else {
                origin = Json.str(source, "path");
            }

            require(remote(origin), "Invalid resolved Android source");
            boolean tree = Json.str(item, "type", "file").equals("tree");
            List<String> files =
                    tree
                            ? members(
                                    origin,
                                    Processes.run(
                                                    adb,
                                                    List.of(
                                                            "-s", serial, "shell", "find", origin,
                                                            "-type", "f"),
                                                    root,
                                                    Duration.ofSeconds(30),
                                                    false)
                                            .stdout())
                            : List.of("");
            Map<String, Object> hashes = new TreeMap<>();

            for (String member : files) {
                Path target =
                        Storage.confined(
                                originals,
                                relative(Json.str(item, "file"))
                                        + (member.isEmpty() ? "" : "/" + member));
                Storage.directory(target.getParent());
                space(root, reserve);
                Processes.run(
                        adb,
                        List.of(
                                "-s",
                                serial,
                                "pull",
                                origin + (member.isEmpty() ? "" : "/" + member),
                                target.toString()),
                        root,
                        Duration.ofMinutes(5),
                        false);
                require(
                        Files.isRegularFile(target)
                                && !Files.isSymbolicLink(target)
                                && Files.size(target) <= 1L << 30,
                        "Bounded ordinary acquired file required");
                total += Files.size(target);
                require(total <= 8L << 30, "Android acquisition exceeds8GiB");
                hashes.put(member.isEmpty() ? "file" : member, FilesSupport.hash(target));
            }

            Map<String, Object> record = new LinkedHashMap<>();
            record.put("file", Json.str(item, "file"));
            record.put("source", source);
            record.put("sha256", tree ? hashes : hashes.get("file"));

            if (tree) {
                record.put("type", "tree");
            }

            android.put(Json.str(item, "name"), record);
        }
    }

    private static void retainedQnxImage(
            String name,
            Path local,
            String profile,
            Path partitions,
            List<Object> qnx,
            List<Object> missing)
            throws Exception {
        Path retained = Storage.confined(local, "profiles/" + profile + "/qnx/" + name),
                archive = Storage.confined(local, "profiles/" + profile + "/update.zip");

        if (Files.isRegularFile(retained)) {
            require(
                    Files.size(retained) > 0 && Files.size(retained) <= 4L << 30,
                    "Bounded owner QNX input required");
            String pin = FilesSupport.hash(retained);
            Files.copy(retained, partitions.resolve(name));
            require(
                    FilesSupport.hash(partitions.resolve(name)).equals(pin)
                            && FilesSupport.hash(retained).equals(pin),
                    "Retained QNX input changed");
            qnx.add(
                    Map.of(
                            "file",
                            name,
                            "bytes",
                            Files.size(retained),
                            "sha256",
                            pin,
                            "source",
                            "retained-owner-input"));
        } else if (Files.isRegularFile(archive)) {
            String pin = FilesSupport.hash(archive);
            var record =
                    new LinkedHashMap<>(
                            QnxOriginalsMain.image(archive, name, partitions.resolve(name), ""));
            require(FilesSupport.hash(archive).equals(pin), "Owner update changed");
            record.put("update_sha256", pin);
            qnx.add(record);
        } else {
            missing.add(name);
        }
    }

    private static Map<String, Object> vehicleQnxImage(
            Map<String, Object> item,
            Map<String, Object> endpoint,
            Path adb,
            String serial,
            Path root,
            Path partitions,
            long reserve)
            throws Exception {
        String origin = Json.str(item, "vehiclePath"), name = Json.str(item, "file");
        Path target = partitions.resolve(name);

        space(root, reserve);

        try (QnxFileClient client =
                new QnxFileClient(
                        adb,
                        serial,
                        Json.str(endpoint, "host"),
                        (int) Json.integer(endpoint, "port"),
                        720)) {
            client.begin();
            QnxFileClient.Open opened = client.open(origin);
            writePartition(client, opened, target, root, reserve);
            client.closeFile(opened.fd());
        }

        return Map.of(
                "file",
                name,
                "vehiclePath",
                origin,
                "bytes",
                Files.size(target),
                "sha256",
                FilesSupport.hash(target));
    }

    private static void updateDraftProfile(
            Path draft, Path directory, String token, Map<String, Object> android)
            throws Exception {
        Map<String, Object> next = Json.object(Json.parse(Files.readString(draft)));
        Map<String, Object> declared =
                next.containsKey("original") ? Json.obj(next, "original") : new TreeMap<>();

        declared.putAll(android);
        next.put("original", declared);
        next.put(
                "acquisitionStatus",
                "captured_inputs_require_android11_and_windows_port_validation");
        Path pending = directory.resolve(".profile-" + token + ".json");
        next.put("status", "incomplete");
        fresh(pending, next);
        Files.move(pending, draft, StandardCopyOption.REPLACE_EXISTING);
    }

    private static void updateQnxProfile(Path directory, String token, List<Object> qnx)
            throws Exception {
        Path qnxProfile = directory.resolve("profile-qnx.json");
        Map<String, Object> qnxDefinition =
                Files.exists(qnxProfile)
                        ? Json.object(Json.parse(Files.readString(qnxProfile)))
                        : new LinkedHashMap<>(Map.of("schema", 1));
        Map<String, Object> pins = new TreeMap<>();

        for (Object value : qnx) {
            Map<String, Object> image = Json.object(value);
            String name = Json.str(image, "file");
            pins.put(name, Map.of("file", name, "sha256", Json.str(image, "sha256")));
        }

        qnxDefinition.put("originals", pins);
        qnxDefinition.put("status", "incomplete");
        Path pendingQnx = directory.resolve(".profile-qnx-" + token + ".json");
        fresh(pendingQnx, qnxDefinition);
        Files.move(pendingQnx, qnxProfile, StandardCopyOption.REPLACE_EXISTING);
    }

    public static void main(String[] args) {
        try {
            require(args.length == 2, "VehicleCaptureMain REQUEST.json RESULT.json");
            Path input = Path.of(args[0]);
            require(input.isAbsolute() && Files.size(input) <= 131072, "Bounded request required");
            Map<String, Object> request = Json.object(Json.parse(Files.readString(input)));
            Path result = Path.of(args[1]);
            Path local = Path.of(Json.str(request, "root")).resolve(".local/build/tmp");
            require(
                    result.isAbsolute() && result.startsWith(local),
                    "Result must be project-local");
            Storage.confined(result, ".");
            fresh(result, run(request));
            System.out.println("Vehicle capture completed; originals and new SHA retained");
        } catch (Failure error) {
            System.err.println(error.code + ": " + error.getMessage());
            System.exit(error.exitCode);
        } catch (Throwable error) {
            // checkstyle-allow IllegalCatch: process boundary reports every failure
            PreparationMain.diagnostic(error, System.err);
            System.exit(1);
        }
    }
}
