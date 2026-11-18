package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class QnxOriginalsTest {
    static void check(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static void archive(Path path, Map<String, byte[]> files) throws Exception {
        try (ZipOutputStream zip = new ZipOutputStream(Files.newOutputStream(path))) {
            for (var entry : files.entrySet()) {
                zip.putNextEntry(new ZipEntry(entry.getKey()));
                zip.write(entry.getValue());
                zip.closeEntry();
            }
        }
    }

    public static void main(String[] args) throws Exception {
        Path root =
                Files.createTempDirectory(
                                Path.of(System.getProperty("java.io.tmpdir")),
                                "voyemu-update-fixture-")
                        .toRealPath();

        try {
            String profile = "fixture-qnx";
            Path directory = root.resolve("profiles/" + profile),
                    inputs = root.resolve(".local/profiles/" + profile),
                    originals = inputs.resolve("qnx");
            Files.createDirectories(directory);
            Files.createDirectories(originals);
            Files.createDirectories(root.resolve(".local/build/tmp"));
            Map<String, byte[]> bytes = new LinkedHashMap<>();
            Map<String, Object> pins = new LinkedHashMap<>();

            for (String name :
                    new String[] {
                        "hyp_la.img", "ifs2_la.img", "system_la.img", "ifs2_recovery_la.img"
                    }) {
                byte[] data = new byte[131072];
                data[0] = (byte) name.length();
                Path temp = root.resolve(name);
                Files.write(temp, data);
                pins.put(name, Map.of("file", name, "sha256", FilesSupport.hash(temp)));
                bytes.put("firmware/" + name, data);
            }

            Path definition = directory.resolve("profile-qnx.json");
            Files.writeString(definition, Json.write(Map.of("schema", 1, "originals", pins)));
            Path zip = inputs.resolve("update.zip");
            archive(zip, bytes);
            Path retained = originals.resolve("hyp_la.img");
            Files.write(retained, bytes.get("firmware/hyp_la.img"));
            var before = Files.getLastModifiedTime(retained);
            Map<String, Object> request =
                    Map.of(
                            "schema",
                            1,
                            "root",
                            root.toString(),
                            "profile",
                            profile,
                            "profile_sha256",
                            FilesSupport.hash(definition),
                            "update_sha256",
                            FilesSupport.hash(zip));
            var result = QnxOriginalsMain.run(request);
            check(
                    ((Number) result.get("originals_materialized")).intValue() == 3,
                    "existing original was unnecessarily replaced");
            check(
                    Files.getLastModifiedTime(retained).equals(before),
                    "existing original metadata changed");

            for (var entry : pins.entrySet()) {
                check(
                        FilesSupport.hash(originals.resolve(entry.getKey()))
                                .equals(Json.str(Json.object(entry.getValue()), "sha256")),
                        "extracted IMG SHA differs");
            }

            Map<String, byte[]> duplicate =
                    Map.of("one/hyp_la.img", new byte[] {1}, "two/hyp_la.img", new byte[] {1});
            Path bad = inputs.resolve("ambiguous.zip");
            archive(bad, duplicate);
            Path target = root.resolve("never-created.img");
            boolean failed = false;

            try {
                QnxOriginalsMain.image(bad, "hyp_la.img", target, "");
            } catch (Failure error) {
                failed = true;
            }

            check(failed && !Files.exists(target), "ambiguous IMG was extracted");
            Files.writeString(retained, "different owner original");
            String changed = FilesSupport.hash(retained);
            failed = false;

            try {
                QnxOriginalsMain.run(request);
            } catch (Failure error) {
                failed = true;
            }

            check(
                    failed && FilesSupport.hash(retained).equals(changed),
                    "wrong existing original was overwritten");
            System.out.println(
                    "Owner update ZIP subset, sparse extent/SHA, ambiguity and original"
                            + " nonreplacement tests passed");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }
    }
}
