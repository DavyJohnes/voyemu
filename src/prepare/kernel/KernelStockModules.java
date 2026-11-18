package com.voyemu.prepare;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.CRC32;
import java.util.zip.Inflater;

/** Verifies the API 30 stock module ABI without loading or modifying the modules. */
final class KernelStockModules {
    private static final int RAMDISK_LIMIT = 64 * 1024 * 1024;
    private static final int ELF_SECTION_SIZE = 64;
    private static final int ELF_SYMBOL_SIZE = 24;
    private static final int VERSION_SIZE = 64;

    static void inventory(Path source, Path output) throws Exception {
        byte[] raw;

        try (var input = Files.newInputStream(source)) {
            raw = input.readNBytes(RAMDISK_LIMIT + 1);
        }

        require(raw.length <= RAMDISK_LIMIT, "Compressed ramdisk exceeds budget");
        byte[] bytes = decompress(raw);
        Map<String, Object> modules = new LinkedHashMap<>();
        String modulesLoad = null;

        for (int at = 0; at < bytes.length; ) {
            while (at < bytes.length && bytes[at] == 0) {
                at++;
            }

            if (at == bytes.length) {
                break;
            }

            require(
                    bytes.length - at >= 110 && text(bytes, at, 6).equals("070701"),
                    "Invalid CPIO header");
            long length = Long.parseUnsignedLong(text(bytes, at + 54, 8), 16);
            long nameLength = Long.parseUnsignedLong(text(bytes, at + 94, 8), 16);
            require(
                    nameLength >= 1 && nameLength <= 4096 && nameLength <= bytes.length - at - 110,
                    "CPIO name out of bounds");
            int nameEnd = at + 110 + (int) nameLength;
            require(bytes[nameEnd - 1] == 0, "Missing CPIO name terminator");
            String name = text(bytes, at + 110, (int) nameLength - 1);
            int data = align(nameEnd);
            require(length <= bytes.length - data, "CPIO data out of bounds");
            at = align(data + (int) length);
            require(at <= bytes.length, "Missing CPIO alignment padding");

            if (name.equals("lib/modules/modules.load")) {
                modulesLoad = text(bytes, data, (int) length);
            }

            if (!name.startsWith("lib/modules/") || !name.endsWith(".ko")) {
                continue;
            }

            require(!modules.containsKey(name), "Duplicate module path " + name);
            modules.put(name, module(Arrays.copyOfRange(bytes, data, data + (int) length)));
        }

        require(modules.size() >= 30 && modulesLoad != null, "Complete stock modules required");
        Map<String, Object> result = new LinkedHashMap<>();
        result.put("schema", 1);
        result.put("source", source.toString());
        result.put("source_sha256", sha256(raw));
        result.put("gzip_members", 2);
        result.put("cpio_bytes", bytes.length);
        result.put("modules_load", modulesLoad);
        result.put("modules", modules);
        Files.writeString(output, Json.write(result) + "\n");

        Set<String> vermagic = new LinkedHashSet<>();
        int imports = 0;

        for (Object value : modules.values()) {
            var info = Json.object(value);
            vermagic.add(Json.str(info, "vermagic"));
            imports += Json.obj(info, "versions").size();
        }

        System.out.printf(
                "%d modules, %d versioned imports, vermagic=%s%n",
                modules.size(), imports, vermagic);
    }

    static void verifyAbi(Path stock, Path symvers, Path releaseFile) throws Exception {
        var modules = Json.obj(Json.object(Json.parse(Files.readString(stock))), "modules");
        String release = Files.readString(releaseFile).strip();

        require(release.matches("[0-9A-Za-z_.+-]+"), "Invalid kernel release");
        Set<String> expected = new LinkedHashSet<>();
        Map<String, String> exports = new LinkedHashMap<>();

        for (Object value : modules.values()) {
            var info = Json.object(value);
            expected.add(Json.str(info, "vermagic").strip().split("\\s+", 2)[0]);
            var versions = Json.obj(info, "exported_versions");

            for (String symbol : versions.keySet()) {
                addExport(
                        exports,
                        symbol,
                        Json.str(versions, symbol),
                        "Conflicting stock module export ");
            }
        }

        require(
                expected.equals(Set.of(release)),
                "Kernel release differs: " + release + "; expected " + expected);

        try (var reader = Files.newBufferedReader(symvers)) {
            for (String line; (line = reader.readLine()) != null; ) {
                if (line.isBlank()) {
                    continue;
                }

                String[] fields = line.strip().split("\\s+", -1);
                require(
                        fields.length >= 4 && fields[0].matches("0x[0-9a-fA-F]{8}"),
                        "Invalid symbol version line");
                addExport(
                        exports,
                        fields[1],
                        fields[0].toLowerCase(Locale.ROOT),
                        "Conflicting duplicate symbol ");
            }
        }

        require(!exports.isEmpty(), "Empty kernel symbol versions");
        int checked = 0;

        for (var module : modules.entrySet()) {
            var imports = Json.obj(Json.object(module.getValue()), "versions");

            for (String symbol : imports.keySet()) {
                String crc = Json.str(imports, symbol);
                String actual = exports.get(symbol);
                require(actual != null, module.getKey() + ": missing exported symbol " + symbol);
                require(
                        actual.equals(crc),
                        module.getKey() + ": CRC differs " + symbol + ": " + actual + " != " + crc);
                checked++;
            }
        }

        System.out.printf(
                "PASS: %d stock modules; %d imported symbol CRCs; release %s; CFI runtime"
                        + " acceptance still required%n",
                modules.size(), checked, release);
    }

    private static byte[] decompress(byte[] raw) throws Exception {
        var bytes = new ByteArrayOutputStream();
        var fields = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
        byte[] chunk = new byte[64 * 1024];
        int at = 0;

        for (int member = 0; member < 2; member++) {
            at = gzipHeader(raw, fields, at);
            CRC32 crc = new CRC32();
            int startSize = bytes.size();
            Inflater inflater = new Inflater(true);

            try {
                inflater.setInput(raw, at, raw.length - at);

                while (!inflater.finished()) {
                    int count = inflater.inflate(chunk);
                    require(count > 0 || inflater.finished(), "Incomplete gzip member");
                    require(
                            count <= RAMDISK_LIMIT - bytes.size(),
                            "Ramdisk extraction budget exceeded");
                    bytes.write(chunk, 0, count);
                    crc.update(chunk, 0, count);
                }

                at = raw.length - inflater.getRemaining();
            } finally {
                inflater.end();
            }

            require(raw.length - at >= 8, "Truncated gzip trailer");
            require(
                    unsignedInt(fields, at) == crc.getValue()
                            && fields.getInt(at + 4) == bytes.size() - startSize,
                    "Invalid gzip checksum or size");
            at += 8;
        }

        require(at == raw.length, "Unconsumed compressed bytes");

        return bytes.toByteArray();
    }

    private static int gzipHeader(byte[] raw, ByteBuffer fields, int start) {
        require(
                raw.length - start >= 10
                        && (raw[start] & 255) == 0x1f
                        && (raw[start + 1] & 255) == 0x8b
                        && raw[start + 2] == 8,
                "Invalid gzip header");
        int flags = raw[start + 3] & 255;
        require((flags & 0xe0) == 0, "Invalid gzip flags");
        int at = start + 10;

        if ((flags & 4) != 0) {
            require(raw.length - at >= 2, "Truncated gzip extra field");
            at += 2 + unsignedShort(fields, at);
            require(at <= raw.length, "Truncated gzip extra field");
        }

        if ((flags & 8) != 0) {
            at = nul(raw, at, raw.length) + 1;
        }

        if ((flags & 16) != 0) {
            at = nul(raw, at, raw.length) + 1;
        }

        if ((flags & 2) != 0) {
            require(raw.length - at >= 2, "Truncated gzip header checksum");
            CRC32 crc = new CRC32();
            crc.update(raw, start, at - start);
            require(
                    (crc.getValue() & 0xffff) == unsignedShort(fields, at),
                    "Invalid gzip header checksum");
            at += 2;
        }

        return at;
    }

    private static Map<String, Object> module(byte[] data) throws Exception {
        require(
                data.length >= 64
                        && data[0] == 0x7f
                        && text(data, 1, 3).equals("ELF")
                        && data[4] == 2
                        && data[5] == 1,
                "ELF64 little-endian module required");
        var elf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        long tableOffset = elf.getLong(40);
        int sectionSize = unsignedShort(elf, 58);
        int sectionCount = unsignedShort(elf, 60);
        int namesIndex = unsignedShort(elf, 62);
        require(
                sectionSize == ELF_SECTION_SIZE
                        && sectionCount >= 1
                        && sectionCount <= 512
                        && namesIndex < sectionCount
                        && inBounds(data.length, tableOffset, (long) sectionSize * sectionCount),
                "ELF section table out of bounds");
        int table = (int) tableOffset;
        byte[] names = section(data, elf, table + namesIndex * sectionSize);
        Map<String, byte[]> selected = new LinkedHashMap<>();
        Map<String, String> exports = new LinkedHashMap<>();

        for (int index = 0; index < sectionCount; index++) {
            int header = table + index * sectionSize;
            long nameOffset = unsignedInt(elf, header);
            require(nameOffset < names.length, "ELF section name out of bounds");
            String name = string(names, (int) nameOffset, names.length);

            if (name.equals(".modinfo") || name.equals("__versions")) {
                require(!selected.containsKey(name), "Duplicate ELF section " + name);
                selected.put(name, section(data, elf, header));
            }

            if (elf.getInt(header + 4) == 2) {
                long link = unsignedInt(elf, header + 40);
                require(link < sectionCount, "ELF string table index out of bounds");
                byte[] strings = section(data, elf, table + (int) link * sectionSize);
                readExports(section(data, elf, header), strings, exports);
            }
        }

        byte[] info = selected.get(".modinfo");
        byte[] versions = selected.get("__versions");
        require(info != null && versions != null, "Required ELF metadata section missing");
        String vermagic = null;

        for (int at = 0; at < info.length; ) {
            int end = nul(info, at, info.length);
            String item = text(info, at, end - at);

            if (item.startsWith("vermagic=")) {
                vermagic = item.substring("vermagic=".length());
                break;
            }

            at = end + 1;
        }

        require(vermagic != null, "Missing vermagic");
        require(versions.length % VERSION_SIZE == 0, "Invalid version entry size");
        var versionData = ByteBuffer.wrap(versions).order(ByteOrder.LITTLE_ENDIAN);
        Map<String, String> imports = new LinkedHashMap<>();

        for (int at = 0; at < versions.length; at += VERSION_SIZE) {
            String symbol = string(versions, at + 8, at + VERSION_SIZE);
            require(
                    !symbol.isEmpty() && !imports.containsKey(symbol),
                    "Invalid or duplicate module symbol " + symbol);
            imports.put(symbol, crc(versionData.getLong(at)));
        }

        Map<String, Object> result = new LinkedHashMap<>();
        result.put("bytes", data.length);
        result.put("sha256", sha256(data));
        result.put("vermagic", vermagic);
        result.put("versions", imports);
        result.put("exported_versions", exports);

        return result;
    }

    private static void readExports(byte[] symbols, byte[] strings, Map<String, String> exports) {
        require(symbols.length % ELF_SYMBOL_SIZE == 0, "Invalid ELF symbol table size");
        var entries = ByteBuffer.wrap(symbols).order(ByteOrder.LITTLE_ENDIAN);

        for (int at = 0; at < symbols.length; at += ELF_SYMBOL_SIZE) {
            long offset = unsignedInt(entries, at);
            require(offset < strings.length, "ELF symbol name out of bounds");
            String name = string(strings, (int) offset, strings.length);

            if (!name.startsWith("__crc_")) {
                continue;
            }

            String symbol = name.substring("__crc_".length());
            require(symbol.matches("[A-Za-z_][A-Za-z0-9_]*"), "Invalid exported name " + name);
            require(unsignedShort(entries, at + 6) == 0xfff1, "Absolute exported CRC required");
            addExport(exports, symbol, crc(entries.getLong(at + 8)), "Conflicting module export ");
        }
    }

    private static byte[] section(byte[] data, ByteBuffer elf, int header) {
        long offset = elf.getLong(header + 24);
        long size = elf.getLong(header + 32);

        require(inBounds(data.length, offset, size), "ELF section data out of bounds");

        return Arrays.copyOfRange(data, (int) offset, (int) (offset + size));
    }

    private static boolean inBounds(int end, long offset, long size) {
        return offset >= 0 && offset <= end && size >= 0 && size <= end - offset;
    }

    private static int nul(byte[] bytes, int start, int end) {
        for (int at = start; at < end; at++) {
            if (bytes[at] == 0) {
                return at;
            }
        }

        throw new Failure("KERNEL_MODULE_ABI", "Missing binary string terminator", 3);
    }

    private static String string(byte[] bytes, int start, int end) {
        return text(bytes, start, nul(bytes, start, end) - start);
    }

    private static String text(byte[] bytes, int start, int size) {
        return new String(bytes, start, size, StandardCharsets.UTF_8);
    }

    private static int unsignedShort(ByteBuffer bytes, int at) {
        return Short.toUnsignedInt(bytes.getShort(at));
    }

    private static long unsignedInt(ByteBuffer bytes, int at) {
        return Integer.toUnsignedLong(bytes.getInt(at));
    }

    private static int align(int value) {
        return (value + 3) & ~3;
    }

    private static String sha256(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    private static String crc(long value) {
        return String.format(Locale.ROOT, "0x%08x", value);
    }

    private static void addExport(
            Map<String, String> exports, String symbol, String crc, String message) {
        String previous = exports.putIfAbsent(symbol, crc);

        require(previous == null || previous.equals(crc), message + symbol);
    }

    private static void require(boolean condition, String message) {
        Failure.require(condition, "KERNEL_MODULE_ABI", message, 3);
    }
}
