package com.voyemu.prepare;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Comparator;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.Map;

final class FilesSupport {
    static String hash(Path path) throws IOException, NoSuchAlgorithmException {
        Failure.require(
                Files.isRegularFile(path) && !Files.isSymbolicLink(path),
                "INPUT_FILE_INVALID",
                "Expected regular input file",
                3);
        MessageDigest digest = MessageDigest.getInstance("SHA-256");

        try (var stream = Files.newInputStream(path)) {
            byte[] buffer = new byte[128 * 1024];

            for (int n; (n = stream.read(buffer)) >= 0; ) {
                if (n > 0) {
                    digest.update(buffer, 0, n);
                }
            }
        }

        return HexFormat.of().formatHex(digest.digest());
    }

    static Map<String, Path> tree(Path root) throws Exception {
        Map<String, Path> result = new LinkedHashMap<>();

        Failure.require(
                Files.isDirectory(root) && !Files.isSymbolicLink(root),
                "INPUT_TREE_INVALID",
                "Expected ordinary input directory",
                3);

        try (var paths = Files.walk(root)) {
            for (Path path : paths.sorted().toList()) {
                Failure.require(
                        !Files.isSymbolicLink(path),
                        "INPUT_TREE_INVALID",
                        "Symbolic link in input tree",
                        3);

                if (Files.isRegularFile(path)) {
                    String relative =
                            root.relativize(path)
                                    .toString()
                                    .replace(java.io.File.separatorChar, '/');
                    Storage.confined(root, relative);
                    Failure.require(
                            result.size() < 100000, "INPUT_TREE_LIMIT", "Too many input files", 3);
                    result.put(relative, path);
                }
            }
        }

        return result;
    }

    static void copyTree(Path source, Path target) throws Exception {
        Storage.directory(target);

        for (var entry : tree(source).entrySet()) {
            Path destination = Storage.confined(target, entry.getKey());
            Storage.directory(destination.getParent());
            Files.copy(entry.getValue(), destination, StandardCopyOption.COPY_ATTRIBUTES);
        }
    }

    static void removeOwnedTree(Path root) throws Exception {
        Failure.require(
                !Files.isSymbolicLink(root), "PATH_SYMLINK", "Refusing symlink cleanup root", 3);

        if (!Files.exists(root)) {
            return;
        }

        try (var paths = Files.walk(root)) {
            for (Path path : paths.sorted(Comparator.reverseOrder()).toList()) {
                Files.delete(path);
            }
        }
    }
}
