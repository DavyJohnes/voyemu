package com.voyemu.prepare;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;

/** Guarded UTF-16 binary XML append; existing indexes and chunks stay intact. */
final class PassionMcuManifest {
    // Byte carrier only; record equality and hashing are never used.
    @SuppressWarnings("ArrayRecordComponent")
    record Chunk(int type, byte[] bytes) {}

    static final String PROVIDER = "com.voyemu.vehicle.PassionMcuProvider",
            AUTHORITY = "com.voyemu.passion.mcu";

    static byte[] add(byte[] input) throws Exception {
        return append(input, List.of(PROVIDER), true);
    }

    private static final class ElementScan {
        boolean application, owner, originalService;
        int insertion = -1, line;
    }

    private static byte[] append(byte[] input, List<String> libraries, boolean service)
            throws Exception {
        String tagName = "provider";

        requireInput(input, libraries, service);
        List<Chunk> chunks = chunks(input);
        List<Chunk> pools = chunks.stream().filter(chunk -> chunk.type() == 1).toList();
        require(pools.size() == 1 && pools.get(0).bytes().length >= 28, "string pool");
        byte[] pool = pools.get(0).bytes();
        int count = u32(pool, 8), start = u32(pool, 20);
        requireUnstyledPool(pool, count, start);
        List<String> strings = strings(pool, count, start);
        int namespace = strings.indexOf("http://schemas.android.com/apk/res/android"),
                name = strings.indexOf("name");
        List<Chunk> maps = chunks.stream().filter(chunk -> chunk.type() == 0x180).toList();
        require(namespace >= 0 && name >= 0 && maps.size() == 1, "android:name");
        byte[] map = maps.get(0).bytes();
        require(
                map.length >= 12 + name * 4 && u32(map, 8 + name * 4) == 0x01010003,
                "name resource ID");
        ElementScan scan = scanElements(chunks, strings, namespace, name, libraries, tagName);
        require(
                scan.application && scan.insertion >= 0 && scan.owner && scan.originalService,
                "original owner/application/service missing");
        List<byte[]> additions = new ArrayList<>();
        List<Integer> offsets = new ArrayList<>();
        int tag = intern(tagName, strings, additions, offsets, pool.length - start);
        int exported = strings.indexOf("exported");

        if (service) {
            require(
                    exported >= 0
                            && map.length >= 12 + exported * 4
                            && u32(map, 8 + exported * 4) == 0x01010010,
                    "exported resource ID");
        }

        int authorities = strings.indexOf("authorities"),
                permission = strings.indexOf("permission");
        require(
                authorities >= 0
                        && permission >= 0
                        && map.length >= 12 + authorities * 4
                        && u32(map, 8 + authorities * 4) == 0x01010018,
                "authority resource ID");
        byte[] newMap = Arrays.copyOf(map, Math.max(map.length, 12 + permission * 4));
        buffer(newMap).putInt(4, newMap.length).putInt(8 + permission * 4, 0x01010006);
        int providerName = intern(PROVIDER, strings, additions, offsets, pool.length - start),
                authorityValue =
                        intern(AUTHORITY, strings, additions, offsets, pool.length - start),
                permissionValue =
                        intern(
                                "android.permission.DUMP",
                                strings,
                                additions,
                                offsets,
                                pool.length - start);
        byte[] provider =
                element(
                        tag,
                        namespace,
                        scan.line,
                        new int[][] {
                            {name, providerName, 0},
                            {permission, permissionValue, 0},
                            {exported, -1, 1},
                            {authorities, authorityValue, 0}
                        });
        List<byte[]> elements = List.of(provider, close(provider));
        byte[] newPool = rebuiltPool(pool, start, strings.size(), additions, offsets);
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        output.write(input, 0, 8);

        for (int i = 0; i < chunks.size(); i++) {
            if (i == scan.insertion) {
                for (byte[] element : elements) {
                    output.write(element);
                }
            }

            output.write(
                    chunks.get(i).type() == 1
                            ? newPool
                            : chunks.get(i).type() == 0x180 ? newMap : chunks.get(i).bytes());
        }

        byte[] result = output.toByteArray();
        buffer(result).putInt(4, result.length);

        return result;
    }

    private static void requireInput(byte[] input, List<String> libraries, boolean service) {
        require(
                input != null && input.length >= 8 && input.length <= 16 * 1024 * 1024,
                "XML extent");
        require(
                !libraries.isEmpty()
                        && new HashSet<>(libraries).size() == libraries.size()
                        && libraries.stream()
                                .allMatch(name ->
                                        service
                                                ? name.matches(
                                                        "[A-Za-z_$][A-Za-z0-9_$]*(?:\\.[A-Za-z_$][A-Za-z0-9_$]*)+")
                                                : name.matches("[A-Za-z][A-Za-z0-9._-]{0,127}")),
                "library names");
        require(
                u16(input, 0) == 3 && u16(input, 2) == 8 && u32(input, 4) == input.length,
                "XML header");
    }

    private static List<Chunk> chunks(byte[] input) {
        List<Chunk> chunks = new ArrayList<>();

        for (int offset = 8; offset < input.length; ) {
            require(offset + 8 <= input.length, "chunk header");
            int size = u32(input, offset + 4), header = u16(input, offset + 2);
            require(size >= header && header >= 8 && size <= input.length - offset, "chunk extent");
            chunks.add(
                    new Chunk(
                            u16(input, offset), Arrays.copyOfRange(input, offset, offset + size)));
            offset += size;
        }

        return chunks;
    }

    private static void requireUnstyledPool(byte[] pool, int count, int start) {
        require(
                count <= 100000
                        && u16(pool, 2) == 28
                        && u32(pool, 12) == 0
                        && u32(pool, 16) == 0
                        && u32(pool, 24) == 0
                        && start == 28 + count * 4
                        && start <= pool.length,
                "unstyled UTF-16 pool");
    }

    private static List<String> strings(byte[] pool, int count, int start) {
        List<String> strings = new ArrayList<>();

        for (int i = 0; i < count; i++) {
            int relative = u32(pool, 28 + i * 4);
            require(relative <= pool.length - start - 2, "string offset");
            int offset = start + relative,
                    length = u16(pool, offset),
                    end = offset + 2 + length * 2;
            require(
                    length < 0x8000 && end + 2 <= pool.length && u16(pool, end) == 0,
                    "string extent");
            strings.add(new String(pool, offset + 2, length * 2, StandardCharsets.UTF_16LE));
        }

        return strings;
    }

    private static ElementScan scanElements(
            List<Chunk> chunks,
            List<String> strings,
            int namespace,
            int name,
            List<String> libraries,
            String tagName) {
        ElementScan scan = new ElementScan();

        for (int i = 0; i < chunks.size(); i++) {
            Chunk chunk = chunks.get(i);

            if (chunk.type() != 0x102 && chunk.type() != 0x103) {
                continue;
            }

            byte[] bytes = chunk.bytes();
            require(bytes.length >= (chunk.type() == 0x102 ? 36 : 24), "element extent");
            int elementIndex = u32(bytes, 20);
            require(elementIndex < strings.size(), "element name");
            String element = strings.get(elementIndex);

            if (chunk.type() == 0x102) {
                requireOwnerElement(element, bytes, strings, scan);
            }

            if (element.equals("application")) {
                if (chunk.type() == 0x102) {
                    require(!scan.application, "multiple applications");
                    scan.application = true;
                } else {
                    require(scan.application && scan.insertion == -1, "application end");
                    scan.insertion = i;
                    scan.line = u32(bytes, 8);
                }
            }

            if (element.equals(tagName) && chunk.type() == 0x102) {
                requireUndeclaredLibraries(bytes, strings, namespace, name, libraries);
            }
        }

        return scan;
    }

    private static void requireOwnerElement(
            String element, byte[] bytes, List<String> strings, ElementScan scan) {
        int attrStart = 16 + u16(bytes, 24), attrSize = u16(bytes, 26), attrCount = u16(bytes, 28);

        require(attrSize == 20 && attrCount <= (bytes.length - attrStart) / 20, "owner attributes");
        java.util.Map<String, String> attrs = new java.util.HashMap<>();

        for (int j = 0; j < attrCount; j++) {
            int at = attrStart + j * 20;
            int key = u32(bytes, at + 4);
            require(key < strings.size(), "owner key");

            if (bytes[at + 15] == 3) {
                int value = u32(bytes, at + 16);
                require(value < strings.size(), "owner value");
                attrs.put(strings.get(key), strings.get(value));
            }
        }

        if (element.equals("manifest")) {
            require(
                    "com.mega.car".equals(attrs.get("package"))
                            && "android.uid.system".equals(attrs.get("sharedUserId")),
                    "original system UID owner");
            scan.owner = true;
        }

        if (element.equals("application")) {
            require(
                    "com.mega.car.CarServiceApplication".equals(attrs.get("name"))
                            && !attrs.containsKey("process"),
                    "original application process");
        }

        if (element.equals("service") && "com.mega.car.MegaCarService".equals(attrs.get("name"))) {
            require(!attrs.containsKey("process"), "original service process");
            scan.originalService = true;
        }

        if (element.equals("provider")) {
            require(
                    !AUTHORITY.equals(attrs.get("authorities"))
                            && !PROVIDER.equals(attrs.get("name")),
                    "provider already declared");
        }
    }

    private static void requireUndeclaredLibraries(
            byte[] bytes, List<String> strings, int namespace, int name, List<String> libraries) {
        int attributeStart = 16 + u16(bytes, 24),
                attributeSize = u16(bytes, 26),
                attributeCount = u16(bytes, 28);

        require(
                attributeSize == 20 && attributeCount <= (bytes.length - attributeStart) / 20,
                "attributes");

        for (int j = 0; j < attributeCount; j++) {
            int offset = attributeStart + j * 20;

            if (u32(bytes, offset) == namespace && u32(bytes, offset + 4) == name) {
                require(bytes[offset + 15] == 3, "library value type");
                int index = u32(bytes, offset + 16);
                require(
                        index < strings.size() && !libraries.contains(strings.get(index)),
                        "library already declared");
            }
        }
    }

    private static byte[] rebuiltPool(
            byte[] pool, int start, int count, List<byte[]> additions, List<Integer> offsets) {
        int dataSize =
                pool.length - start + additions.stream().mapToInt(bytes -> bytes.length).sum();
        int newStart = start + offsets.size() * 4;
        byte[] newPool = new byte[(newStart + dataSize + 3) & ~3];

        System.arraycopy(pool, 0, newPool, 0, start);
        ByteBuffer buffer = buffer(newPool);
        buffer.putInt(4, newPool.length);
        buffer.putInt(8, count);
        buffer.putInt(20, newStart);

        for (int i = 0; i < offsets.size(); i++) {
            buffer.putInt(start + i * 4, offsets.get(i));
        }

        System.arraycopy(pool, start, newPool, newStart, pool.length - start);
        int cursor = newStart + pool.length - start;

        for (byte[] addition : additions) {
            System.arraycopy(addition, 0, newPool, cursor, addition.length);
            cursor += addition.length;
        }

        return newPool;
    }

    private static byte[] element(int tag, int namespace, int line, int[][] attributes) {
        byte[] element = new byte[36 + attributes.length * 20];
        ByteBuffer buffer = buffer(element);

        buffer.putShort(0, (short) 0x102);
        buffer.putShort(2, (short) 16);
        buffer.putInt(4, element.length);
        buffer.putInt(8, line);
        buffer.putInt(12, -1);
        buffer.putInt(16, -1);
        buffer.putInt(20, tag);
        buffer.putShort(24, (short) 20);
        buffer.putShort(26, (short) 20);
        buffer.putShort(28, (short) attributes.length);

        for (int i = 0; i < attributes.length; i++) {
            int at = 36 + i * 20;
            int[] attribute = attributes[i];
            boolean bool = attribute[2] != 0;
            buffer.putInt(at, namespace);
            buffer.putInt(at + 4, attribute[0]);
            buffer.putInt(at + 8, bool ? -1 : attribute[1]);
            buffer.putShort(at + 12, (short) 8);
            buffer.put(at + 15, (byte) (bool ? 0x12 : 3));
            buffer.putInt(at + 16, attribute[1]);
        }

        return element;
    }

    private static byte[] close(byte[] start) {
        byte[] end = Arrays.copyOf(start, 24);

        buffer(end).putShort(0, (short) 0x103).putInt(4, 24);

        return end;
    }

    private static int intern(
            String text,
            List<String> strings,
            List<byte[]> additions,
            List<Integer> offsets,
            int originalSize) {
        int index = strings.indexOf(text);

        if (index >= 0) {
            return index;
        }

        byte[] encoded = text.getBytes(StandardCharsets.UTF_16LE);
        byte[] addition = new byte[encoded.length + 4];
        buffer(addition).putShort(0, (short) text.length());
        System.arraycopy(encoded, 0, addition, 2, encoded.length);
        offsets.add(originalSize + additions.stream().mapToInt(bytes -> bytes.length).sum());
        additions.add(addition);
        strings.add(text);

        return strings.size() - 1;
    }

    private static ByteBuffer buffer(byte[] bytes) {
        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    }

    private static int u16(byte[] bytes, int offset) {
        require(offset >= 0 && offset + 2 <= bytes.length, "truncated field");

        return Short.toUnsignedInt(buffer(bytes).getShort(offset));
    }

    private static int u32(byte[] bytes, int offset) {
        require(offset >= 0 && offset + 4 <= bytes.length, "truncated field");
        int value = buffer(bytes).getInt(offset);
        require(value >= 0, "integer extent");

        return value;
    }

    private static void require(boolean condition, String reason) {
        Failure.require(
                condition, "PATCH_MANIFEST_INVALID", "Invalid binary manifest: " + reason, 5);
    }
}
