package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Locale;

public final class KernelSourceTest {
    private static void expect(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void write(Path file, String text) throws Exception {
        Files.createDirectories(file.getParent());
        Files.writeString(file, text, StandardCharsets.ISO_8859_1);
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toAbsolutePath();
        Path tree = root.resolve("tree");

        write(tree.resolve("src/x.txt"), "one\ntwo\nthree\nfour\n");
        write(tree.resolve("src-b.txt"), "n\n");
        write(tree.resolve("run.sh"), "#!/bin/sh\n");

        if (!System.getProperty("os.name").toLowerCase(Locale.ROOT).startsWith("windows")) {
            Files.setPosixFilePermissions(
                    tree.resolve("run.sh"), PosixFilePermissions.fromString("rwxr-xr-x"));
            // Expected values come from git write-tree and the sorted manifest of this fixture.
            String[] identity = KernelSource.identity(tree);
            expect(
                    identity[0].equals("cf1daccb1593242796dadbb9edaabf8aa262b3e6"),
                    "Git tree id differs: " + identity[0]);
            expect(
                    identity[1].equals(
                            "b7ace364d7ee2dba0b3dc6dc2e4ae7c3db6351a52249e6a311aa9cd1fe4c70c8"),
                    "Content manifest differs: " + identity[1]);
        }

        // The second hunk applies at a shifted position after the first adds a line.
        Path diff = root.resolve("change.patch");
        write(
                diff,
                """
                --- a/src/x.txt
                +++ b/src/x.txt
                @@ -1,2 +1,3 @@
                 one
                +one-and-half
                 two
                @@ -2,3 +3,3 @@
                 two
                -three
                +THREE
                 four
                """);
        KernelSource.patch(tree, diff);
        expect(
                Files.readString(tree.resolve("src/x.txt"))
                        .equals("one\none-and-half\ntwo\nTHREE\nfour\n"),
                "Patched text differs");

        // A patch whose context no longer matches, including a reapplied one, is rejected.
        String before = Files.readString(tree.resolve("src/x.txt"));
        Path stale = root.resolve("stale.patch");
        write(
                stale,
                "--- a/src/x.txt\n+++ b/src/x.txt\n@@ -2,3 +2,3 @@\n two\n-three\n+3\n four\n");
        boolean rejected = false;

        try {
            KernelSource.patch(tree, stale);
        } catch (Failure expected) {
            rejected = true;
        }

        expect(rejected, "Non-matching hunk accepted");
        expect(Files.readString(tree.resolve("src/x.txt")).equals(before), "Rejected patch wrote");
        System.out.println("KernelSourceTest PASS");
    }
}
