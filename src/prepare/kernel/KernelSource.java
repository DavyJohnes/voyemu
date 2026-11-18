package com.voyemu.prepare;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
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
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.regex.Pattern;

/**
 * Kernel source by commit. Gitiles archives embed the request time, so the archive bytes are not a
 * stable pin; the extracted tree is verified by its Git tree id and a SHA-256 content manifest.
 */
final class KernelSource {
    private static final long MAX_ARCHIVE_BYTES = 512L * 1024 * 1024;
    private static final Pattern HUNK =
            Pattern.compile("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@");

    private KernelSource() {}

    static void extract(Path project, Map<String, Object> source, Path destination)
            throws Exception {
        Path archive = Storage.confined(project, Json.str(source, "archive_file"));

        if (Files.notExists(archive, LinkOption.NOFOLLOW_LINKS)) {
            download(URI.create(Json.str(source, "url")), archive);
        }

        KernelHostTools.command(
                null, null, "tar", "-xzf", archive.toString(), "-C", destination.toString());
        String[] identity = identity(destination);
        Failure.require(
                identity[0].equals(Json.str(source, "tree"))
                        && identity[1].equals(Json.str(source, "content_sha256")),
                "KERNEL_SOURCE",
                "Kernel source tree differs from its pins: tree "
                        + identity[0]
                        + ", content "
                        + identity[1]
                        + "; remove "
                        + archive
                        + " to download it again",
                3);
    }

    private static void download(URI url, Path archive) throws Exception {
        Failure.require(
                "https".equals(url.getScheme()) && url.getUserInfo() == null,
                "KERNEL_SOURCE",
                "HTTPS source URL required",
                3);
        Files.createDirectories(archive.getParent());
        Path temporary = archive.resolveSibling("." + UUID.randomUUID() + ".partial");
        HttpClient client =
                HttpClient.newBuilder()
                        .connectTimeout(Duration.ofSeconds(30))
                        .followRedirects(HttpClient.Redirect.NORMAL)
                        .build();

        try {
            var response =
                    client.send(
                            HttpRequest.newBuilder(url)
                                    .timeout(Duration.ofMinutes(30))
                                    .header("User-Agent", "voyemu/1.0.0")
                                    .GET()
                                    .build(),
                            HttpResponse.BodyHandlers.ofInputStream());

            try (InputStream input = response.body()) {
                Failure.require(
                        response.statusCode() == 200,
                        "KERNEL_SOURCE",
                        "Source download returned HTTP " + response.statusCode(),
                        3);
                long size = 0;

                try (var output = Files.newOutputStream(temporary)) {
                    byte[] buffer = new byte[128 * 1024];

                    for (int n; (n = input.read(buffer)) >= 0; ) {
                        size += n;
                        Failure.require(
                                size <= MAX_ARCHIVE_BYTES,
                                "KERNEL_SOURCE",
                                "Source archive exceeds size limit",
                                3);
                        output.write(buffer, 0, n);
                    }
                }
            }

            Files.move(temporary, archive, StandardCopyOption.ATOMIC_MOVE);
        } finally {
            Files.deleteIfExists(temporary);
        }
    }

    /** Returns the Git tree id and the SHA-256 of the sorted "mode sha256 path" lines. */
    static String[] identity(Path root) throws Exception {
        var manifest = new ArrayList<String>();
        String tree = tree(root, "", manifest);

        manifest.sort(null);
        var digest = MessageDigest.getInstance("SHA-256");

        for (String line : manifest) {
            digest.update(line.getBytes(StandardCharsets.UTF_8));
        }

        return new String[] {tree, HexFormat.of().formatHex(digest.digest())};
    }

    private static String tree(Path directory, String prefix, List<String> manifest)
            throws Exception {
        List<Path> children;

        try (var list = Files.list(directory)) {
            children = new ArrayList<>(list.toList());
        }

        // Git orders entries by name bytes, with a trailing slash for directories.
        children.sort((a, b) -> {
            String x = gitName(a), y = gitName(b);

            return compareBytes(
                    x.getBytes(StandardCharsets.UTF_8), y.getBytes(StandardCharsets.UTF_8));
        });
        var entries = new ByteArrayOutputStream();

        for (Path child : children) {
            String name = child.getFileName().toString();
            String path = prefix + name;
            String mode;
            byte[] id;

            if (Files.isSymbolicLink(child)) {
                byte[] target =
                        Files.readSymbolicLink(child).toString().getBytes(StandardCharsets.UTF_8);
                mode = "120000";
                id = object("blob", target);
                manifest.add(mode + " " + sha256(target) + " " + path + "\n");
            } else if (Files.isDirectory(child, LinkOption.NOFOLLOW_LINKS)) {
                mode = "40000";
                id = HexFormat.of().parseHex(tree(child, path + "/", manifest));
            } else {
                Failure.require(
                        Files.isRegularFile(child, LinkOption.NOFOLLOW_LINKS),
                        "KERNEL_SOURCE",
                        "Unexpected source entry " + path,
                        3);
                boolean executable =
                        Files.getPosixFilePermissions(child)
                                .contains(PosixFilePermission.OWNER_EXECUTE);
                mode = executable ? "100755" : "100644";
                byte[] content = Files.readAllBytes(child);
                id = object("blob", content);
                manifest.add(mode + " " + sha256(content) + " " + path + "\n");
            }

            entries.write((mode + " " + name + "\0").getBytes(StandardCharsets.UTF_8));
            entries.write(id);
        }

        return HexFormat.of().formatHex(object("tree", entries.toByteArray()));
    }

    private static String gitName(Path path) {
        String name = path.getFileName().toString();

        return Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS) ? name + "/" : name;
    }

    private static int compareBytes(byte[] a, byte[] b) {
        for (int i = 0; i < Math.min(a.length, b.length); i++) {
            int c = Integer.compare(a[i] & 0xff, b[i] & 0xff);

            if (c != 0) {
                return c;
            }
        }

        return Integer.compare(a.length, b.length);
    }

    private static byte[] object(String type, byte[] content) throws Exception {
        var digest = MessageDigest.getInstance("SHA-1");

        digest.update((type + " " + content.length + "\0").getBytes(StandardCharsets.US_ASCII));

        return digest.digest(content);
    }

    private static String sha256(byte[] content) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(content));
    }

    /**
     * Applies a unified diff whose hunks match exactly at their recorded or a shifted position;
     * fuzzy context and reversed patches are rejected.
     */
    static void patch(Path source, Path diff) throws IOException {
        List<String> lines = Files.readAllLines(diff, StandardCharsets.ISO_8859_1);
        int i = 0;

        while (i < lines.size()) {
            if (!lines.get(i).startsWith("--- ")) {
                i++;
                continue;
            }

            Failure.require(
                    i + 1 < lines.size() && lines.get(i + 1).startsWith("+++ b/"),
                    "KERNEL_PATCH",
                    "Malformed patch header in " + diff.getFileName(),
                    3);
            String target = lines.get(i + 1).substring(6);
            int tab = target.indexOf('\t');
            String name = tab < 0 ? target : target.substring(0, tab);
            Path file = Storage.confined(source, name);
            List<String> text =
                    new ArrayList<>(Files.readAllLines(file, StandardCharsets.ISO_8859_1));
            int shift = 0;
            i += 2;

            while (i < lines.size() && lines.get(i).startsWith("@@ ")) {
                var header = HUNK.matcher(lines.get(i));
                Failure.require(
                        header.find(),
                        "KERNEL_PATCH",
                        "Malformed hunk header in " + diff.getFileName(),
                        3);
                int start = Integer.parseInt(header.group(1));
                int oldCount = header.group(2) == null ? 1 : Integer.parseInt(header.group(2));
                int newCount = header.group(4) == null ? 1 : Integer.parseInt(header.group(4));
                i++;
                var before = new ArrayList<String>();
                var after = new ArrayList<String>();

                while (before.size() < oldCount || after.size() < newCount) {
                    Failure.require(
                            i < lines.size(),
                            "KERNEL_PATCH",
                            "Truncated hunk in " + diff.getFileName(),
                            3);
                    String line = lines.get(i++);

                    if (line.startsWith("\\")) {
                        continue;
                    }

                    char kind = line.isEmpty() ? ' ' : line.charAt(0);
                    Failure.require(
                            kind == ' ' || kind == '-' || kind == '+',
                            "KERNEL_PATCH",
                            "Malformed hunk in " + diff.getFileName(),
                            3);
                    String body = line.isEmpty() ? "" : line.substring(1);

                    if (kind != '+') {
                        before.add(body);
                    }

                    if (kind != '-') {
                        after.add(body);
                    }
                }

                int at = find(text, before, Math.max(0, start - 1 + shift));
                Failure.require(
                        at >= 0,
                        "KERNEL_PATCH",
                        diff.getFileName() + " does not apply to " + name,
                        3);
                text.subList(at, at + before.size()).clear();
                text.addAll(at, after);
                shift = at - (start - 1) + after.size() - before.size();
            }

            Files.writeString(file, String.join("\n", text) + "\n", StandardCharsets.ISO_8859_1);
        }
    }

    private static int find(List<String> text, List<String> block, int hint) {
        for (int distance = 0; distance <= text.size(); distance++) {
            for (int at : new int[] {hint - distance, hint + distance}) {
                if (at >= 0
                        && at + block.size() <= text.size()
                        && text.subList(at, at + block.size()).equals(block)) {
                    return at;
                }
            }
        }

        return -1;
    }
}
