package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class ArchivesTest {
    static Path zip(Path file, String... entries) throws Exception {
        try (var output = new ZipOutputStream(Files.newOutputStream(file))) {
            for (String entry : entries) {
                output.putNextEntry(new ZipEntry(entry));
                output.write("own fixture".getBytes(StandardCharsets.UTF_8));
                output.closeEntry();
            }
        }

        return file;
    }

    static void rejected(Path archive, Path output, long limit, String code) throws Exception {
        try {
            Archives.extract(archive, output, limit, false);
            throw new AssertionError("Archive accepted");
        } catch (Failure expected) {
            if (!expected.code.equals(code)) {
                throw expected;
            }
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("voyemu-native-archive-test-").toRealPath();

        try {
            Archives.extract(
                    zip(root.resolve("ok.zip"), "directory/file"), root.resolve("ok"), 100, false);
            StorageJsonTest.check(
                    Files.readString(root.resolve("ok/directory/file")).equals("own fixture"));
            rejected(
                    zip(root.resolve("escape.zip"), "../escape"),
                    root.resolve("escape"),
                    100,
                    "ARCHIVE_PATH_INVALID");
            rejected(
                    zip(root.resolve("case.zip"), "File", "file"),
                    root.resolve("case"),
                    100,
                    "ARCHIVE_COLLISION");
            rejected(
                    zip(root.resolve("limit.zip"), "file"),
                    root.resolve("limit"),
                    3,
                    "ARCHIVE_SIZE_LIMIT");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println("ZIP extraction guards passed");
    }
}
