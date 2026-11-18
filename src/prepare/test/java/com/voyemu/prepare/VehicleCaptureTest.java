package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class VehicleCaptureTest {
    static void check(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    interface Probe {
        void run() throws Exception;
    }

    static void rejected(Probe probe) throws Exception {
        boolean failed = false;

        try {
            probe.run();
        } catch (Failure error) {
            failed = true;
        }

        check(failed, "unsafe capture accepted");
    }

    static Map<String, Object> spec(String profile) {
        return Map.of(
                "schema",
                1,
                "profile",
                profile,
                "android",
                List.of(
                        Map.of(
                                "name",
                                "Fixture",
                                "file",
                                "app.apk",
                                "source",
                                Map.of("package", "com.fixture.app"))),
                "qnx",
                Map.of(
                        "host",
                        "172.16.100.22",
                        "port",
                        8000,
                        "images",
                        List.of(
                                Map.of(
                                        "file",
                                        "ifs2_recovery_la.img",
                                        "partitionLabel",
                                        "ifs2_recovery"))));
    }

    public static void main(String[] args) throws Exception {
        var valid = spec("fixture-a11");

        VehicleCaptureMain.validate(valid, "fixture-a11");
        rejected(() -> VehicleCaptureMain.validate(valid, "another-profile"));
        rejected(() -> VehicleCaptureMain.relative("../escape"));
        rejected(() -> VehicleCaptureMain.relative("a//b"));
        rejected(() ->
                VehicleCaptureMain.members(
                        "/system/framework", "/system/framework/a.jar\n/data/escape\n"));
        rejected(() ->
                VehicleCaptureMain.members(
                        "/system/framework", "/system/framework/A.jar\n/system/framework/a.jar\n"));
        check(!VehicleCaptureMain.remote("/system/../data/app.apk"), "remote traversal accepted");

        if (System.getProperty("os.name").toLowerCase(Locale.ROOT).contains("windows")) {
            System.out.println("Capture manifest guards passed; POSIX fixture skipped on Windows");

            return;
        }

        Path root =
                Files.createTempDirectory(
                                Path.of(System.getProperty("java.io.tmpdir")),
                                "voyemu-capture-fixture-")
                        .toRealPath();

        try {
            Path directory = root.resolve("profiles/fixture-a11");
            Files.createDirectories(directory);
            Path capture = directory.resolve("capture.json"),
                    draft = directory.resolve("profile.json");
            Files.writeString(capture, Json.write(valid));
            Files.writeString(draft, Json.write(Map.of("schema", 1, "original", Map.of())));
            Path adb = root.resolve("adb-fixture");
            Files.writeString(
                    adb,
                    "#!/bin/sh\n"
                            + "case \"$3\" in\n"
                            + "get-state) printf 'device\\n"
                            + "';;\n"
                            + "shell) printf 'package:/system/app/fixture.apk\\n"
                            + "';;\n"
                            + "pull) printf 'acquired fixture bytes' > \"$5\";;\n"
                            + "*) exit 1;;\n"
                            + "esac\n");
            Files.setPosixFilePermissions(adb, PosixFilePermissions.fromString("rwx------"));
            Path old = root.resolve(".local/profiles/fixture-a11/originals/old.apk");
            Files.createDirectories(old.getParent());
            Files.writeString(old, "preserve original");
            Map<String, Object> request =
                    new LinkedHashMap<>(
                            Map.of(
                                    "schema",
                                    1,
                                    "root",
                                    root.toString(),
                                    "profile",
                                    "fixture-a11",
                                    "serial",
                                    "fixture-car",
                                    "adb",
                                    adb.toString(),
                                    "adb_sha256",
                                    FilesSupport.hash(adb),
                                    "capture_sha256",
                                    FilesSupport.hash(capture),
                                    "draft_sha256",
                                    FilesSupport.hash(draft)));
            var unsafe = new LinkedHashMap<>(request);
            unsafe.put("serial", "emulator-5554");
            // The fixture pulls a few bytes; a 64 MiB reserve keeps the check on any disk.
            long reserve = 64L << 20;
            rejected(() -> VehicleCaptureMain.run(unsafe, reserve));
            var result = VehicleCaptureMain.run(request, reserve);
            check(
                    Boolean.FALSE.equals(result.get("vehicle_modified"))
                            && Boolean.FALSE.equals(result.get("runtime_accepted")),
                    "capture overclaimed vehicle/runtime changes");
            check(
                    Json.list(result.get("missing_qnx_roles"))
                            .equals(List.of("ifs2_recovery_la.img")),
                    "unresolved recovery silently guessed");
            check(
                    Files.readString(root.resolve(".local/profiles/fixture-a11/originals/app.apk"))
                            .equals("acquired fixture bytes"),
                    "adb pull output changed");
            check(Files.readString(old).equals("preserve original"), "prior owner originals lost");
            check(
                    !Files.exists(root.resolve(".local/inputs-preserved")),
                    "capture created an implicit backup");
            Path imported = root.resolve(".local/profiles/fixture-a11/originals/app.apk");
            Path conflict = root.resolve("conflict");
            Files.createDirectory(conflict);
            Files.writeString(conflict.resolve("app.apk"), "different acquisition");
            rejected(() -> VehicleCaptureMain.validateImport(conflict, imported.getParent()));
            check(
                    Files.readString(imported).equals("acquired fixture bytes"),
                    "conflicting capture overwrote original");
            var updated = Json.object(Json.parse(Files.readString(draft)));
            check(
                    Json.str(Json.obj(Json.obj(updated, "original"), "Fixture"), "sha256")
                            .equals(
                                    FilesSupport.hash(
                                            root.resolve(
                                                    ".local/profiles/fixture-a11/originals/app.apk"))),
                    "new draft SHA missing");
            var qnx =
                    Json.object(
                            Json.parse(Files.readString(directory.resolve("profile-qnx.json"))));
            check(
                    Json.str(qnx, "status").equals("incomplete")
                            && Json.obj(qnx, "originals").isEmpty(),
                    "missing QNX roles falsely became a runnable profile");

            try (var files = Files.list(root.resolve(".local/profiles/fixture-a11"))) {
                check(
                        files.filter(p -> p.getFileName().toString().startsWith("capture-cluster-"))
                                        .count()
                                == 1,
                        "capture provenance not retained beside inputs");
            }

            check(
                    !Files.exists(root.resolve(".local/profiles/fixture-a11.capture.lock")),
                    "capture lock leaked");
            System.out.println(
                    "Capture guards, fake-adb acquisition, new SHA, unresolved recovery and"
                            + " originals preservation passed; no device contacted");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }
    }
}
