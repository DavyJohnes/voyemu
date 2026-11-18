package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.Adler32;

public final class HiddenApiAlignmentTest {
    static void need(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static int hidden(byte[] b) {
        var q = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);
        int m = q.getInt(52);

        for (int i = 0; i < q.getInt(m); i++) {
            int p = m + 4 + 12 * i;

            if ((q.getShort(p) & 65535) == 0xf000) {
                return q.getInt(p + 8);
            }
        }

        throw new AssertionError();
    }

    static Map<String, String> semantics(byte[] b) throws Exception {
        Map<String, String> m = new TreeMap<>();

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), b).getClasses()) {
            m.put(c.getType(), FrameworkDexMaterializer.semantic(c));
        }

        return m;
    }

    public static void main(String[] args) throws Exception {
        byte[] before = Files.readAllBytes(Path.of(args[0]));

        need((hidden(before) & 3) != 0);
        byte[] after = HiddenApiOffsets.alignHiddenApi(before);
        int h = hidden(before), aligned = hidden(after);
        need((aligned & 3) == 0 && after.length == before.length + 4);
        var b = ByteBuffer.wrap(before).order(ByteOrder.LITTLE_ENDIAN);
        var a = ByteBuffer.wrap(after).order(ByteOrder.LITTLE_ENDIAN);
        int size = b.getInt(h);
        need(
                Arrays.equals(
                        Arrays.copyOfRange(before, h, h + size),
                        Arrays.copyOfRange(after, aligned, aligned + size)));
        need(semantics(before).equals(semantics(after)));
        need(Arrays.equals(after, HiddenApiOffsets.alignHiddenApi(after)));
        need(
                Arrays.equals(
                        Arrays.copyOfRange(after, 12, 32),
                        MessageDigest.getInstance("SHA-1")
                                .digest(Arrays.copyOfRange(after, 32, after.length))));
        Adler32 ad = new Adler32();
        ad.update(after, 12, after.length - 12);
        need(a.getInt(8) == (int) ad.getValue());

        for (int i = h; i < aligned; i++) {
            need(after[i] == 0);
        }

        byte[] invalid = before.clone();
        ByteBuffer.wrap(invalid).order(ByteOrder.LITTLE_ENDIAN).putInt(h, Integer.MAX_VALUE);
        boolean rejected = false;

        try {
            HiddenApiOffsets.alignHiddenApi(invalid);
        } catch (RuntimeException expected) {
            rejected = true;
        }

        need(rejected);
        Files.write(Path.of(args[1]), after);
        System.out.println(
                "PASS actual ART-failing DEX: aligned hidden API, identical metadata and semantics,"
                        + " checksums, idempotence and invalid size rejection");
    }
}
