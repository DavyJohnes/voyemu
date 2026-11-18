package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Extracts declared OEM IMG inputs from the owner's local update.zip, without patching them. */
public final class QnxOriginalsMain {
    static void require(boolean value, String reason) {
        Failure.require(value, "QNX_ORIGINALS", reason, 3);
    }

    static Map<String, Object> image(Path archive, String name, Path target, String expected)
            throws Exception {
        require(
                name.matches("[A-Za-z0-9_-]+\\.img")
                        && (expected.isEmpty() || expected.matches("[a-f0-9]{64}")),
                "Explicit IMG role and SHA required");
        Storage.confined(archive, ".");
        Storage.confined(target, ".");
        require(
                Files.isRegularFile(archive) && Files.size(archive) <= 16L << 30,
                "Bounded owner update.zip required");
        ZipEntry selected = null;

        try (ZipFile zip = new ZipFile(archive.toFile())) {
            var entries = zip.entries();
            int entryCount = 0;

            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                require(++entryCount <= 100000, "Update inventory exceeds bound");
                String path = entry.getName();

                if (!entry.isDirectory() && (path.equals(name) || path.endsWith("/" + name))) {
                    require(selected == null, "Ambiguous IMG role in update.zip");
                    VehicleCaptureMain.relative(path);
                    selected = entry;
                }
            }

            require(
                    selected != null && selected.getSize() > 0 && selected.getSize() <= 4L << 30,
                    "Expected IMG is absent or exceeds4GiB");

            try (var input = zip.getInputStream(selected);
                    FileChannel out =
                            FileChannel.open(
                                    target,
                                    StandardOpenOption.CREATE_NEW,
                                    StandardOpenOption.WRITE,
                                    StandardOpenOption.SPARSE)) {
                byte[] bytes = new byte[65536];
                long extent = 0;

                for (int count; (count = input.read(bytes)) >= 0; ) {
                    if (count == 0) {
                        continue;
                    }

                    require(extent + count <= selected.getSize(), "IMG exceeds declared extent");
                    boolean zero = true;

                    for (int i = 0; i < count; i++) {
                        if (bytes[i] != 0) {
                            zero = false;
                            break;
                        }
                    }

                    if (!zero) {
                        out.position(extent);
                        ByteBuffer buffer = ByteBuffer.wrap(bytes, 0, count);

                        while (buffer.hasRemaining()) {
                            out.write(buffer);
                        }
                    }

                    extent += count;

                    if (extent % (64L << 20) == 0) {
                        require(
                                Files.getFileStore(target).getUsableSpace() >= 20L << 30,
                                "20GiB disk reserve reached");
                    }
                }

                require(extent == selected.getSize(), "IMG is truncated");

                if (out.size() < extent) {
                    out.position(extent - 1);
                    out.write(ByteBuffer.wrap(new byte[] {0}));
                }

                out.force(true);
            }

            String sha = FilesSupport.hash(target);
            require(
                    expected.isEmpty() || sha.equals(expected),
                    "Extracted IMG differs from profile SHA");

            return Map.of(
                    "file",
                    name,
                    "bytes",
                    Files.size(target),
                    "sha256",
                    sha,
                    "source",
                    "owner-update",
                    "entry",
                    selected.getName());
        }
    }

    static Map<String, Object> run(Map<String, Object> request) throws Exception {
        require(Json.integer(request, "schema") == 1, "Schema1 required");
        Path root = Path.of(Json.str(request, "root"));
        String profile = Json.str(request, "profile");
        require(
                root.isAbsolute()
                        && root.toRealPath().equals(root)
                        && profile.matches("[a-z0-9][a-z0-9._-]+"),
                "Canonical project/profile required");
        Path definition = Storage.confined(root, "profiles/" + profile + "/profile-qnx.json");
        require(
                FilesSupport.hash(definition).equals(Json.str(request, "profile_sha256")),
                "QNX profile changed");
        Map<String, Object> originals =
                Json.obj(Json.object(Json.parse(Files.readString(definition))), "originals");
        require(originals.size() == 4, "Four original IMG roles required");
        Path archive = Storage.confined(root, ".local/profiles/" + profile + "/update.zip");
        String archiveSha = FilesSupport.hash(archive);
        require(archiveSha.equals(Json.str(request, "update_sha256")), "Owner update.zip changed");
        Path output = Storage.confined(root, ".local/profiles/" + profile + "/qnx");
        Storage.directory(output);
        Path stage = Storage.confined(root, ".local/build/tmp/qnx-originals-" + UUID.randomUUID());
        Files.createDirectory(stage);
        Map<String, Object> records = new LinkedHashMap<>();
        boolean success = false;

        try {
            for (var item : originals.entrySet()) {
                String name = item.getKey();
                Map<String, Object> spec = Json.object(item.getValue());
                require(
                        name.matches("[A-Za-z0-9_-]+\\.img") && Json.str(spec, "file").equals(name),
                        "Fixed original IMG path required");
                Path existing = Storage.confined(output, name);
                String sha = Json.str(spec, "sha256");

                if (Files.exists(existing)) {
                    require(
                            Files.isRegularFile(existing)
                                    && FilesSupport.hash(existing).equals(sha),
                            "Existing owner IMG differs from profile; preserve it");
                    continue;
                }

                records.put(name, image(archive, name, stage.resolve(name), sha));
            }

            require(
                    FilesSupport.hash(archive).equals(archiveSha)
                            && FilesSupport.hash(definition)
                                    .equals(Json.str(request, "profile_sha256")),
                    "Update/profile changed during extraction");

            for (String name : records.keySet()) {
                Files.move(stage.resolve(name), output.resolve(name));
            }

            Path receipt = output.resolve("update-import-" + UUID.randomUUID() + ".json");
            Files.writeString(
                    receipt,
                    Json.write(
                                    Map.of(
                                            "schema",
                                            1,
                                            "profile",
                                            profile,
                                            "source",
                                            "owner-update",
                                            "update_sha256",
                                            archiveSha,
                                            "images",
                                            records,
                                            "vehicle_modified",
                                            false))
                            + "\n",
                    StandardOpenOption.CREATE_NEW);
            success = true;

            return Map.of(
                    "schema",
                    1,
                    "profile",
                    profile,
                    "originals_materialized",
                    records.size(),
                    "receipt",
                    receipt.toString(),
                    "vehicle_modified",
                    false);
        } finally {
            if (success) {
                Files.delete(stage);
            }
        }
    }

    public static void main(String[] args) {
        try {
            require(args.length == 2, "QnxOriginalsMain REQUEST.json RESULT.json");
            Path input = Path.of(args[0]);
            require(input.isAbsolute() && Files.size(input) <= 131072, "Bounded request required");
            Map<String, Object> request = Json.object(Json.parse(Files.readString(input)));
            Path result = Path.of(args[1]);
            require(
                    result.isAbsolute()
                            && result.startsWith(
                                    Path.of(Json.str(request, "root")).resolve(".local/build/tmp")),
                    "Result must be project-local");
            Storage.confined(result, ".");
            Files.writeString(
                    result, Json.write(run(request)) + "\n", StandardOpenOption.CREATE_NEW);
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
