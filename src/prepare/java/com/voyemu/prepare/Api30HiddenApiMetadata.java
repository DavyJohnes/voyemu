package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Repair dexlib2 2.5.2's hidden-API class order using the exact pinned input's raw member flags.
 */
final class Api30HiddenApiMetadata {
    private record Definition(String type, List<String> members) {}

    private static void require(boolean value) {
        Failure.require(
                value,
                "API30_HIDDEN_API_METADATA",
                "Exact API30 hidden-API metadata preservation required",
                5);
    }

    private static int integer(byte[] bytes, int at) {
        require(at >= 0 && at <= bytes.length - 4);

        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getInt(at);
    }

    private static int unsigned(byte[] bytes, int[] at) {
        int value = 0;

        for (int shift = 0; shift < 35; shift += 7) {
            require(at[0] >= 0 && at[0] < bytes.length);
            int next = bytes[at[0]++] & 255;
            require(shift != 28 || (next & 240) == 0);
            value |= (next & 127) << shift;

            if (next < 128) {
                require(shift == 0 || next != 0);

                return value;
            }
        }

        throw new Failure("API30_HIDDEN_API_METADATA", "Invalid bounded restriction encoding", 5);
    }

    private static int hidden(byte[] bytes) {
        int map = integer(bytes, 52), count = integer(bytes, map), found = 0;

        require(
                map >= 0
                        && map <= bytes.length - 4
                        && count > 0
                        && count < 100
                        && count <= (bytes.length - map - 4) / 12);

        for (int index = 0; index < count; index++) {
            int item = map + 4 + index * 12;
            require(item >= 0 && item <= bytes.length - 12);

            if ((bytes[item] & 255) == 0 && (bytes[item + 1] & 255) == 240) {
                require(found == 0 && integer(bytes, item + 4) == 1);
                found = integer(bytes, item + 8);
            }
        }

        require(found > 0);

        return found;
    }

    private static int align(int offset) {
        require(offset >= 0 && offset <= Integer.MAX_VALUE - 3);

        return (offset + 3) & ~3;
    }

    private static void zeros(byte[] bytes, int start, int end) {
        require(start >= 0 && end >= start && end <= bytes.length);

        for (int at = start; at < end; at++) {
            require(bytes[at] == 0);
        }
    }

    // Android 11 ART CheckIntraSectionIterate aligns hiddenapi_class_data to uint32_t.
    // dexlib2 2.5.2 puts it immediately after variable-width class_data instead.
    // Only the final class_data -> hiddenapi -> map_list layout is relocatable here.
    static byte[] alignedTail(byte[] bytes, boolean repair) {
        require(integer(bytes, 32) == bytes.length && integer(bytes, 36) == 112);
        require(
                integer(bytes, 108) >= 112
                        && integer(bytes, 108) <= bytes.length
                        && integer(bytes, 104) == bytes.length - integer(bytes, 108));
        int map = integer(bytes, 52), count = integer(bytes, map);
        require(
                (map & 3) == 0
                        && count >= 3
                        && count < 100
                        && map >= 112
                        && count <= (bytes.length - map - 4) / 12
                        && map + 4 + count * 12 == bytes.length);
        var layout = mapLayout(bytes, map, count, repair);
        int hidden = layout.hidden();
        int end = classDataEnd(bytes, layout.classData(), layout.classCount(), hidden);
        require(end <= hidden && (hidden == end || hidden == align(end)));
        zeros(bytes, end, hidden);
        int size = integer(bytes, hidden);
        require(size >= 4 && size <= map - hidden);
        require(map == align(hidden + size));
        zeros(bytes, hidden + size, map);

        if (!repair) {
            require((hidden & 3) == 0);
        }

        int newHidden = align(hidden), newMap = align(newHidden + size), extra = newMap - map;

        if (extra == 0) {
            return bytes;
        }

        require(repair && extra == 4 && newHidden > hidden);
        byte[] result = new byte[bytes.length + extra];
        System.arraycopy(bytes, 0, result, 0, hidden);
        System.arraycopy(bytes, hidden, result, newHidden, size);
        System.arraycopy(bytes, map, result, newMap, bytes.length - map);
        var header = ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN);
        header.putInt(32, result.length);
        header.putInt(52, newMap);
        header.putInt(104, integer(bytes, 104) + extra);
        header.putInt(newMap + 4 + (count - 2) * 12 + 8, newHidden);
        header.putInt(newMap + 4 + (count - 1) * 12 + 8, newMap);
        alignedTail(result, false);

        return result;
    }

    private record MapLayout(int classData, int classCount, int hidden) {}

    private static MapLayout mapLayout(byte[] bytes, int map, int count, boolean repair) {
        int previous = -1, classData = 0, classCount = 0, hidden = 0;
        Set<Integer> types = new HashSet<>();

        for (int index = 0; index < count; index++) {
            int item = map + 4 + index * 12,
                    type = (bytes[item] & 255) | ((bytes[item + 1] & 255) << 8);
            int size = integer(bytes, item + 4), offset = integer(bytes, item + 8);
            require(
                    types.add(type)
                            && bytes[item + 2] == 0
                            && bytes[item + 3] == 0
                            && size > 0
                            && offset > previous
                            && offset < bytes.length);
            int width = sectionWidth(type);
            require(byteAligned(type) || (offset & 3) == 0 || (repair && type == 0xf000));
            int limit = index + 1 == count ? bytes.length : integer(bytes, item + 20);
            require(
                    limit > offset
                            && limit <= bytes.length
                            && (width == 0 || size <= (limit - offset) / width));
            requireTailOrder(index, count, type, size, offset, map);

            if (type == 0x2000) {
                classData = offset;
                classCount = size;
            }

            if (type == 0xf000) {
                hidden = offset;
            }

            previous = offset;
        }

        require(classData > 0 && hidden > classData && types.contains(0x1000));

        return new MapLayout(classData, classCount, hidden);
    }

    private static int sectionWidth(int type) {
        return switch (type) {
            case 0 -> 112;
            case 1, 2, 7 -> 4;
            case 3 -> 12;
            case 4, 5, 8 -> 8;
            case 6 -> 32;
            case 0x1000,
                    0x1001,
                    0x1002,
                    0x1003,
                    0x2000,
                    0x2001,
                    0x2002,
                    0x2003,
                    0x2004,
                    0x2005,
                    0x2006,
                    0xf000 ->
                    0;
            default ->
                    throw new Failure(
                            "API30_HIDDEN_API_METADATA", "Unexpected API30 map section", 5);
        };
    }

    private static boolean byteAligned(int type) {
        return type == 0x2000
                || type == 0x2002
                || type == 0x2003
                || type == 0x2004
                || type == 0x2005;
    }

    private static void requireTailOrder(
            int index, int count, int type, int size, int offset, int map) {
        if (type == 0) {
            require(index == 0 && offset == 0 && size == 1);
        }

        if (type == 0x2000) {
            require(index == count - 3);
        }

        if (type == 0xf000) {
            require(index == count - 2 && size == 1);
        }

        if (type == 0x1000) {
            require(index == count - 1 && offset == map && size == 1);
        }
    }

    private static int classDataEnd(byte[] bytes, int classData, int classCount, int hidden) {
        int[] cursor = {classData};

        for (int index = 0; index < classCount; index++) {
            int[] sizes = new int[4];

            for (int group = 0; group < 4; group++) {
                sizes[group] = unsigned(bytes, cursor);
                require(sizes[group] >= 0 && sizes[group] < 10000);
            }

            for (int group = 0; group < 4; group++) {
                for (int member = 0; member < sizes[group]; member++) {
                    unsigned(bytes, cursor);
                    unsigned(bytes, cursor);

                    if (group >= 2) {
                        unsigned(bytes, cursor);
                    }

                    require(cursor[0] <= hidden);
                }
            }
        }

        return cursor[0];
    }

    private static List<Definition> definitions(byte[] bytes) {
        var dex = new DexBackedDexFile(Opcodes.forApi(30), bytes);
        int count = integer(bytes, 96), start = integer(bytes, 100);

        require(count > 0 && count < 10000 && start >= 0 && start <= bytes.length - count * 32);
        List<Definition> result = new ArrayList<>();

        for (int index = 0; index < count; index++) {
            int record = start + index * 32, data = integer(bytes, record + 24);
            int typeIndex = integer(bytes, record);
            require(typeIndex >= 0 && typeIndex < integer(bytes, 64));
            String type = dex.getTypeSection().get(typeIndex);
            List<String> members = new ArrayList<>();

            if (data != 0) {
                int[] at = {data}, counts = new int[4];

                for (int group = 0; group < 4; group++) {
                    counts[group] = unsigned(bytes, at);
                    require(counts[group] >= 0 && counts[group] < 10000);
                }

                for (int group = 0; group < 4; group++) {
                    int memberIndex = 0;

                    for (int member = 0; member < counts[group]; member++) {
                        int delta = unsigned(bytes, at);
                        require(delta >= 0 && memberIndex <= Integer.MAX_VALUE - delta);
                        memberIndex += delta;
                        require(
                                memberIndex >= 0
                                        && memberIndex < integer(bytes, group < 2 ? 80 : 88));
                        unsigned(bytes, at);

                        if (group >= 2) {
                            unsigned(bytes, at);
                        }

                        members.add(
                                group < 2
                                        ? dex.getFieldSection().get(memberIndex).toString()
                                        : dex.getMethodSection().get(memberIndex).toString());
                    }
                }
            }

            result.add(new Definition(type, members));
        }

        return result;
    }

    static byte[] restore(byte[] original, byte[] rewritten) throws Exception {
        require(original != null);
        int sourceHidden = hidden(original), targetHidden = hidden(rewritten);
        Map<String, Integer> flags = new HashMap<>();
        Set<String> explicit = new HashSet<>();
        List<Definition> source = definitions(original);
        int sourceSize = integer(original, sourceHidden);
        require(
                sourceSize >= 4 + source.size() * 4
                        && sourceHidden <= original.length - sourceSize);

        for (int index = 0; index < source.size(); index++) {
            Definition type = source.get(index);
            int offset = integer(original, sourceHidden + 4 + index * 4);
            require(offset == 0 || (offset >= 4 + source.size() * 4 && offset < sourceSize));

            if (offset != 0) {
                explicit.add(type.type());
            }

            int[] at = {sourceHidden + offset};

            for (String member : type.members()) {
                int value = offset == 0 ? 0 : unsigned(original, at);
                require(at[0] <= sourceHidden + sourceSize);
                require((value & 7) <= 5 && (value & ~31) == 0 && flags.put(member, value) == null);
            }
        }

        List<Definition> target = definitions(rewritten);
        int headerSize = 4 + target.size() * 4;
        byte[] table = new byte[headerSize];
        var header = ByteBuffer.wrap(table).order(ByteOrder.LITTLE_ENDIAN);
        ByteArrayOutputStream values = new ByteArrayOutputStream();
        Set<String> seen = new HashSet<>();

        for (int index = 0; index < target.size(); index++) {
            Definition type = target.get(index);

            if (explicit.contains(type.type())) {
                header.putInt(4 + index * 4, headerSize + values.size());
            }

            for (String member : type.members()) {
                Integer flag = flags.get(member);

                if (flag == null) {
                    require(
                            type.type().equals(ViewDumpPatch.HELPER)
                                    || type.type()
                                            .startsWith(
                                                    ViewDumpPatch.HELPER.substring(
                                                                    0,
                                                                    ViewDumpPatch.HELPER.length()
                                                                            - 1)
                                                            + "$"));
                } else {
                    require(seen.add(member));
                }

                if (explicit.contains(type.type())) {
                    require(flag != null);
                    values.write(flag);
                }
            }
        }

        require(seen.equals(flags.keySet()));
        int size = headerSize + values.size();
        require(
                size == integer(rewritten, targetHidden)
                        && targetHidden <= rewritten.length - size);
        header.putInt(0, size);
        byte[] result = rewritten.clone();
        System.arraycopy(table, 0, result, targetHidden, table.length);
        System.arraycopy(
                values.toByteArray(), 0, result, targetHidden + table.length, values.size());
        result = alignedTail(result, true);
        byte[] signature =
                java.security.MessageDigest.getInstance("SHA-1")
                        .digest(java.util.Arrays.copyOfRange(result, 32, result.length));
        System.arraycopy(signature, 0, result, 12, signature.length);
        var checksum = new java.util.zip.Adler32();
        checksum.update(result, 12, result.length - 12);
        ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) checksum.getValue());

        return result;
    }
}
