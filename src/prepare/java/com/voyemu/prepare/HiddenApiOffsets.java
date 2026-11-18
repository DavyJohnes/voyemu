package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.Adler32;

final class HiddenApiOffsets {
    static void need(boolean p) {
        if (!p) {
            Failure.require(p, "HIDDEN_API_CONTRACT", "Exact hidden API metadata required", 3);
        }
    }

    static List<String> members(ClassDef c) {
        List<String> l = new ArrayList<>();

        for (Field f : c.getStaticFields()) {
            l.add("S:" + f);
        }

        for (Field f : c.getInstanceFields()) {
            l.add("I:" + f);
        }

        for (Method m : c.getDirectMethods()) {
            l.add("D:" + m);
        }

        for (Method m : c.getVirtualMethods()) {
            l.add("V:" + m);
        }

        return l;
    }

    static void checkOrder(List<String> a, List<String> b) {
        need(a.equals(b));
    }

    static boolean sameOrder(List<String> a, List<String> b) {
        checkOrder(a, b);

        return true;
    }

    // Only for bytes produced by the pinned DexPool, whose offset entries use sorted class keys.
    static Map<String, List<Long>> rawFlags(
            byte[] b, List<String> order, Map<String, List<String>> members) {
        ByteBuffer x = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);

        need(b.length >= 112 && x.getInt(32) == b.length);
        int n = x.getInt(96), map = x.getInt(52);
        need(n == order.size() && map >= 112 && map + 4L <= b.length);
        var section = hiddenSection(x, b, map, n);
        int h = section.start(), size = section.size();
        Map<String, List<Long>> values = new HashMap<>();
        List<int[]> spans = new ArrayList<>();

        for (int i = 0; i < n; i++) {
            String key = order.get(i);
            need(members.containsKey(key) && !values.containsKey(key));
            int off = x.getInt(h + 4 + 4 * i);
            int[] pos = {off};
            List<Long> v = new ArrayList<>();
            need(off == 0 || (off >= 4 + 4L * n && off < size));

            for (int j = 0; j < members.get(key).size(); j++) {
                long value = 0;

                if (off != 0) {
                    value = flag(b, h, size, pos);
                }

                v.add(value);
            }

            if (off != 0) {
                spans.add(new int[] {off, pos[0]});
            }

            values.put(key, v);
        }

        spans.sort(Comparator.comparingInt(t -> t[0]));
        int end = 4 + 4 * n;

        for (int[] span : spans) {
            need(span[0] >= end);
            end = span[1];
        }

        return values;
    }

    record HiddenSection(int start, int size) {}

    static HiddenSection hiddenSection(ByteBuffer x, byte[] b, int map, int n) {
        int count = x.getInt(map), h = -1;

        need(count > 0 && count < 1000 && map + 4L + 12L * count <= b.length);

        for (int i = 0; i < count; i++) {
            int p = map + 4 + 12 * i;

            if ((x.getShort(p) & 65535) == 0xf000) {
                need(h < 0 && x.getInt(p + 4) == 1);
                h = x.getInt(p + 8);
            }
        }

        need(h >= 112 && h + 4L + 4L * n <= b.length);
        int size = x.getInt(h);
        need(size >= 4 + 4L * n && h + (long) size <= b.length);

        return new HiddenSection(h, size);
    }

    // Reads one ULEB128 flag at h + pos[0] and advances pos[0] past it.
    static long flag(byte[] b, int h, int size, int[] pos) {
        long value = 0;
        int digit = 0, c;

        do {
            need(pos[0] < size && digit < 5);
            c = b[h + pos[0]++] & 255;
            need(digit != 4 || (c & 0xf0) == 0);
            value |= (long) (c & 127) << (7 * digit++);
        } while ((c & 128) != 0);

        return value;
    }

    // ART aligns hiddenapi_class_data to uint32; the pinned writer emits it after variable-size
    // class_data.
    static byte[] alignHiddenApi(byte[] encoded) throws Exception {
        ByteBuffer in = ByteBuffer.wrap(encoded).order(ByteOrder.LITTLE_ENDIAN);

        need(encoded.length >= 112 && in.getInt(32) == encoded.length);
        int map = in.getInt(52);
        need(map >= 112 && map + 4L <= encoded.length);
        int count = in.getInt(map);
        need(count > 1 && count < 1000 && map + 4L + 12L * count == encoded.length);
        int hidden = -1, index = -1;

        for (int i = 0; i < count; i++) {
            int p = map + 4 + 12 * i;

            if ((in.getShort(p) & 65535) == 0xf000) {
                need(hidden < 0 && in.getInt(p + 4) == 1);
                hidden = in.getInt(p + 8);
                index = i;
            }
        }

        need(hidden >= 112 && hidden + 4L <= map && index == count - 2);
        int last = map + 4 + 12 * (count - 1);
        need(
                (in.getShort(last) & 65535) == 0x1000
                        && in.getInt(last + 4) == 1
                        && in.getInt(last + 8) == map);
        int size = in.getInt(hidden);
        need(size >= 4 && hidden + (long) size <= map);

        for (int i = hidden + size; i < map; i++) {
            need(encoded[i] == 0);
        }

        int pad = -hidden & 3;

        if (pad == 0) {
            return encoded.clone();
        }

        need(in.getInt(108) <= hidden && in.getInt(104) == encoded.length - in.getInt(108));
        byte[] result = new byte[encoded.length + 4];
        System.arraycopy(encoded, 0, result, 0, hidden);
        System.arraycopy(encoded, hidden, result, hidden + pad, size);
        int newMap = map + 4;
        System.arraycopy(encoded, map, result, newMap, encoded.length - map);
        ByteBuffer out = ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(32, result.length);
        out.putInt(52, newMap);
        out.putInt(104, in.getInt(104) + 4);
        out.putInt(newMap + 4 + 12 * index + 8, hidden + pad);
        out.putInt(newMap + 4 + 12 * (count - 1) + 8, newMap);
        checksum(result);

        return result;
    }

    // Mirrors Android 11 DexFileVerifier: no gaps or backward offsets in class-def order.
    static void verifySequential(byte[] b) {
        var q = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);
        int map = q.getInt(52), h = -1;

        for (int i = 0; i < q.getInt(map); i++) {
            int p = map + 4 + 12 * i;

            if ((q.getShort(p) & 65535) == 0xf000) {
                h = q.getInt(p + 8);
            }
        }

        need(h >= 112 && (h & 3) == 0);
        int size = q.getInt(h), cursor = 4 + 4 * q.getInt(96), i = 0;

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), b).getClasses()) {
            int off = q.getInt(h + 4 + 4 * i++);

            if (off == 0) {
                continue;
            }

            var classMembers = members(c);
            need(off == cursor && !classMembers.isEmpty());

            for (int j = 0; j < classMembers.size(); j++) {
                int digits = 0, value;

                do {
                    need(cursor < size && digits < 5);
                    value = b[h + cursor++] & 255;
                    need(digits != 4 || (value & 0xf0) == 0);
                    digits++;
                } while ((value & 128) != 0);
            }
        }

        need(cursor == size);
    }

    static void checksum(byte[] b) throws Exception {
        byte[] sha = MessageDigest.getInstance("SHA-1").digest(Arrays.copyOfRange(b, 32, b.length));

        System.arraycopy(sha, 0, b, 12, 20);
        Adler32 a = new Adler32();
        a.update(b, 12, b.length - 12);
        ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) a.getValue());
    }

    static byte[] fix(byte[] original, byte[] encoded) throws Exception {
        byte[] b = alignHiddenApi(encoded);
        ByteBuffer q = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);

        need(b.length >= 112 && q.getInt(32) == b.length);
        int n = q.getInt(96), defs = q.getInt(100), map = q.getInt(52);
        need(
                n >= 0
                        && n < 100000
                        && defs >= 112
                        && (long) defs + 32L * n <= b.length
                        && map >= 112
                        && map + 4L <= b.length);
        var section = hiddenSection(q, b, map, n);
        int h = section.start(), size = section.size();
        var in = new DexBackedDexFile(Opcodes.forApi(30), original);
        var out = new DexBackedDexFile(Opcodes.forApi(30), b);
        Map<String, List<String>> im = new HashMap<>();

        for (ClassDef c : in.getClasses()) {
            need(im.put(c.getType(), members(c)) == null);
        }

        List<String> physical = new ArrayList<>();
        Set<String> seen = new HashSet<>();

        for (ClassDef c : out.getClasses()) {
            need(
                    seen.add(c.getType())
                            && im.containsKey(c.getType())
                            && sameOrder(im.get(c.getType()), members(c)));
            physical.add(c.getType());
        }

        need(physical.size() == n && seen.equals(im.keySet()));
        List<String> sorted = new ArrayList<>(physical);
        Collections.sort(sorted);
        List<String> sourceOrder = new ArrayList<>();

        for (ClassDef c : in.getClasses()) {
            sourceOrder.add(c.getType());
        }

        Map<String, List<Long>> expected = rawFlags(original, sourceOrder, im);
        need(expected.equals(rawFlags(b, sorted, im)));
        Map<String, Integer> offsets = new HashMap<>();

        for (int i = 0; i < n; i++) {
            int off = q.getInt(h + 4 + 4 * i);
            need(off == 0 || (off >= 4 + 4L * n && off < size));
            need(offsets.put(sorted.get(i), off) == null);
        }

        requireDisjointSpans(b, section, n, sorted, offsets, im);
        byte[] packed = Arrays.copyOfRange(b, h, h + size);
        ByteBuffer table = ByteBuffer.wrap(packed).order(ByteOrder.LITTLE_ENDIAN);
        int cursor = 4 + 4 * n;

        for (int i = 0; i < n; i++) {
            String key = physical.get(i);
            int off = offsets.get(key);

            if (off == 0) {
                table.putInt(4 + 4 * i, 0);
                continue;
            }

            int pos = flagsEnd(b, h, size, off, im.get(key).size());
            int length = pos - off;
            need(cursor + length <= size);
            table.putInt(4 + 4 * i, cursor);
            System.arraycopy(b, h + off, packed, cursor, length);
            cursor += length;
        }

        need(cursor == size);
        System.arraycopy(packed, 0, b, h, size);
        need(expected.equals(rawFlags(b, physical, im)));
        verifySequential(b);
        byte[] sha = MessageDigest.getInstance("SHA-1").digest(Arrays.copyOfRange(b, 32, b.length));
        System.arraycopy(sha, 0, b, 12, 20);
        Adler32 a = new Adler32();
        a.update(b, 12, b.length - 12);
        q.putInt(8, (int) a.getValue());

        return b;
    }

    static void requireDisjointSpans(
            byte[] b,
            HiddenSection section,
            int n,
            List<String> sorted,
            Map<String, Integer> offsets,
            Map<String, List<String>> im) {
        List<int[]> spans = new ArrayList<>();

        for (String key : sorted) {
            int off = offsets.get(key);

            if (off == 0) {
                continue;
            }

            int pos = flagsEnd(b, section.start(), section.size(), off, im.get(key).size());
            spans.add(new int[] {off, pos});
        }

        spans.sort(Comparator.comparingInt(x -> x[0]));
        int end = 4 + 4 * n;

        for (int[] span : spans) {
            need(span[0] >= end);
            end = span[1];
        }
    }

    // Returns the offset after count bounded ULEB128 flags starting at off.
    static int flagsEnd(byte[] b, int h, int size, int off, int count) {
        int pos = off;

        for (int j = 0; j < count; j++) {
            int shifts = 0, v;

            do {
                need(pos < size && shifts < 5);
                v = b[h + pos++] & 255;
                shifts++;
            } while ((v & 128) != 0);
        }

        return pos;
    }
}
