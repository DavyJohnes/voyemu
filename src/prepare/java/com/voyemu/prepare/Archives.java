package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.text.Normalizer;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.ZipFile;

/** Bounded ZIP extraction with ordinary files and optional materialized SDK aliases. */
final class Archives {
    static void extract(Path archive, Path root, long byteLimit, boolean toolAliases)
            throws Exception {
        Failure.require(
                !Files.exists(root),
                "ARCHIVE_TARGET_EXISTS",
                "Extraction target already exists",
                3);
        Map<String, Integer> modes = modes(archive);
        Map<String, String> aliases = new LinkedHashMap<>();
        Map<String, String> spelling = new HashMap<>();
        Storage.directory(root);
        long written = 0;

        try (ZipFile zip = new ZipFile(archive.toFile(), StandardCharsets.UTF_8)) {
            var entries = zip.entries();

            while (entries.hasMoreElements()) {
                var entry = entries.nextElement();
                String name = entry.getName();

                if (entry.isDirectory()) {
                    name = name.substring(0, name.length() - 1);
                }

                try {
                    NativeCore.safeEntry(name);
                } catch (IllegalArgumentException e) {
                    throw new Failure("ARCHIVE_PATH_INVALID", "Unsafe ZIP path", 3);
                }

                String fold =
                        Normalizer.normalize(name, Normalizer.Form.NFC)
                                .toLowerCase(java.util.Locale.ROOT);
                Failure.require(
                        !spelling.containsKey(fold), "ARCHIVE_COLLISION", "Colliding ZIP path", 3);
                spelling.put(fold, name);
                Path target = Storage.confined(root, name);
                int mode = modes.getOrDefault(entry.getName(), 0);
                int type = mode & 0170000;
                Failure.require(
                        type == 0 || type == 0100000 || type == 0040000 || type == 0120000,
                        "ARCHIVE_ENTRY_INVALID",
                        "Unsupported ZIP file type",
                        3);

                if (entry.isDirectory()) {
                    Storage.directory(target);
                    continue;
                }

                if (type == 0120000) {
                    Failure.require(
                            toolAliases, "ARCHIVE_SYMLINK", "Symbolic ZIP entry is forbidden", 3);

                    try (var input = zip.getInputStream(entry)) {
                        byte[] bytes = input.readNBytes(4097);
                        Failure.require(
                                bytes.length <= 4096, "ARCHIVE_SYMLINK", "Oversized alias", 3);
                        aliases.put(name, new String(bytes, StandardCharsets.UTF_8));
                    }

                    continue;
                }

                Storage.directory(target.getParent());
                Failure.require(
                        !Files.exists(target), "ARCHIVE_COLLISION", "Existing extraction path", 3);

                try (var input = zip.getInputStream(entry);
                        var output =
                                Files.newOutputStream(
                                        target, java.nio.file.StandardOpenOption.CREATE_NEW)) {
                    byte[] buffer = new byte[128 * 1024];

                    for (int n; (n = input.read(buffer)) >= 0; ) {
                        if (n == 0) {
                            continue;
                        }

                        written += n;
                        Failure.require(
                                written <= byteLimit,
                                "ARCHIVE_SIZE_LIMIT",
                                "ZIP extraction exceeds size limit",
                                3);
                        output.write(buffer, 0, n);
                    }
                }

                if ((mode & 0111) != 0) {
                    Failure.require(
                            target.toFile().setExecutable(true, false),
                            "ARCHIVE_MODE_FAILED",
                            "Cannot preserve executable mode",
                            3);
                }
            }
        }

        for (var alias : aliases.entrySet()) {
            String resolved = alias.getKey();

            for (int depth = 0; aliases.containsKey(resolved); depth++) {
                Failure.require(depth < 32, "ARCHIVE_SYMLINK", "Alias cycle", 3);
                Path relative = Path.of(aliases.get(resolved));
                Failure.require(!relative.isAbsolute(), "ARCHIVE_SYMLINK", "Absolute alias", 3);
                Path parent = Path.of(resolved).getParent();
                Path joined = (parent == null ? relative : parent.resolve(relative)).normalize();
                resolved = joined.toString().replace(java.io.File.separatorChar, '/');
                Storage.confined(root, resolved);
            }

            Path from = Storage.confined(root, resolved);
            Path target = Storage.confined(root, alias.getKey());
            Failure.require(
                    Files.isRegularFile(from),
                    "ARCHIVE_SYMLINK",
                    "Alias has no ordinary target",
                    3);
            written += Files.size(from);
            Failure.require(
                    written <= byteLimit,
                    "ARCHIVE_SIZE_LIMIT",
                    "Materialized ZIP exceeds limit",
                    3);
            Storage.directory(target.getParent());
            Files.copy(from, target, StandardCopyOption.COPY_ATTRIBUTES);
        }
    }

    private static Map<String, Integer> modes(Path archive) throws Exception {
        Map<String, Integer> result = new HashMap<>();

        try (FileChannel file = FileChannel.open(archive)) {
            long size = file.size();
            Failure.require(size >= 22, "ARCHIVE_INVALID", "Truncated ZIP", 3);
            int length = (int) Math.min(size, 65557);
            ByteBuffer tail = ByteBuffer.allocate(length).order(ByteOrder.LITTLE_ENDIAN);
            file.position(size - length);
            read(file, tail);
            int eocd = -1;

            for (int i = length - 22; i >= 0; i--) {
                if (tail.getInt(i) == 0x06054b50
                        && i + 22 + Short.toUnsignedInt(tail.getShort(i + 20)) == length) {
                    eocd = i;
                    break;
                }
            }

            Failure.require(eocd >= 0, "ARCHIVE_INVALID", "ZIP end record missing", 3);
            int count = Short.toUnsignedInt(tail.getShort(eocd + 10));
            long offset = Integer.toUnsignedLong(tail.getInt(eocd + 16));
            Failure.require(
                    count < 65535
                            && offset < 0xffffffffL
                            && tail.getShort(eocd + 4) == 0
                            && tail.getShort(eocd + 6) == 0,
                    "ARCHIVE_FORMAT_UNSUPPORTED",
                    "Split or ZIP64 archive is not supported",
                    3);
            file.position(offset);

            for (int i = 0; i < count; i++) {
                ByteBuffer header = ByteBuffer.allocate(46).order(ByteOrder.LITTLE_ENDIAN);
                read(file, header);
                Failure.require(
                        header.getInt(0) == 0x02014b50,
                        "ARCHIVE_INVALID",
                        "Invalid central directory",
                        3);
                int nameLength = Short.toUnsignedInt(header.getShort(28));
                int extra = Short.toUnsignedInt(header.getShort(30));
                int comment = Short.toUnsignedInt(header.getShort(32));
                Failure.require(
                        (header.getShort(8) & 1) == 0, "ARCHIVE_INVALID", "Encrypted ZIP entry", 3);
                ByteBuffer name = ByteBuffer.allocate(nameLength);
                read(file, name);
                String text = new String(name.array(), StandardCharsets.UTF_8);
                Failure.require(
                        !result.containsKey(text), "ARCHIVE_COLLISION", "Duplicate ZIP entry", 3);
                result.put(text, header.getInt(38) >>> 16);
                file.position(file.position() + extra + comment);
            }
        }

        return result;
    }

    private static void read(FileChannel file, ByteBuffer target) throws Exception {
        while (target.hasRemaining()) {
            Failure.require(file.read(target) > 0, "ARCHIVE_INVALID", "Truncated ZIP metadata", 3);
        }
    }
}
