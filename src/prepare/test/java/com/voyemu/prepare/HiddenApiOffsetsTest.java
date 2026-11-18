package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.Adler32;

public class HiddenApiOffsetsTest {
    static void need(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static void rejects(Runnable r) {
        try {
            r.run();
            throw new AssertionError("accepted invalid fixture");
        } catch (RuntimeException expected) {
            // Rejection is the expected outcome.
        }
    }

    static int section(ByteBuffer b) {
        int m = b.getInt(52);

        for (int i = 0; i < b.getInt(m); i++) {
            int p = m + 4 + 12 * i;

            if ((b.getShort(p) & 65535) == 0xf000) {
                return b.getInt(p + 8);
            }
        }

        throw new AssertionError();
    }

    static void rejectsFix(byte[] in, byte[] dex) {
        rejects(() -> {
            try {
                HiddenApiOffsets.fix(in, dex);
            } catch (RuntimeException e) {
                throw e;
            } catch (Exception e) {
                throw new IllegalArgumentException(e);
            }
        });
    }

    static void rejectsWrappedFix(byte[] in, byte[] dex) {
        rejects(() -> {
            try {
                HiddenApiOffsets.fix(in, dex);
            } catch (Exception e) {
                throw new IllegalArgumentException(e);
            }
        });
    }

    public static void main(String[] a) throws Exception {
        byte[] in = Files.readAllBytes(Path.of(a[0])),
                raw = HiddenApiOffsets.alignHiddenApi(Files.readAllBytes(Path.of(a[1]))),
                fixed = HiddenApiOffsets.fix(in, raw);
        ByteBuffer q = ByteBuffer.wrap(fixed).order(ByteOrder.LITTLE_ENDIAN),
                r = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
        int h = section(q), n = q.getInt(96);

        for (int i = 32; i < fixed.length; i++) {
            if (i < h || i >= h + q.getInt(h)) {
                need(raw[i] == fixed[i]);
            }
        }

        HiddenApiOffsets.verifySequential(fixed);
        rejects(() -> HiddenApiOffsets.verifySequential(raw));
        need(
                Arrays.equals(
                        Arrays.copyOfRange(fixed, 12, 32),
                        MessageDigest.getInstance("SHA-1")
                                .digest(Arrays.copyOfRange(fixed, 32, fixed.length))));
        Adler32 ad = new Adler32();
        ad.update(fixed, 12, fixed.length - 12);
        need(q.getInt(8) == (int) ad.getValue());
        byte[] dup = raw.clone();
        ByteBuffer d = ByteBuffer.wrap(dup).order(ByteOrder.LITTLE_ENDIAN);
        int defs = d.getInt(100);
        d.putInt(defs + 32, d.getInt(defs));
        rejectsFix(in, dup);
        byte[] missing = raw.clone();
        ByteBuffer z = ByteBuffer.wrap(missing).order(ByteOrder.LITTLE_ENDIAN);
        z.putInt(defs, z.getInt(64) - 1);
        rejectsFix(in, missing);
        rejects(() -> HiddenApiOffsets.checkOrder(List.of("S:a", "D:b"), List.of("D:b", "S:a")));
        byte[] invalid = raw.clone();
        ByteBuffer v = ByteBuffer.wrap(invalid).order(ByteOrder.LITTLE_ENDIAN);
        v.putInt(h + 4, v.getInt(h) + 1);
        rejectsFix(in, invalid);
        int first = 0;

        while (first < n && r.getInt(h + 4 + 4 * first) == 0) {
            first++;
        }

        need(first < n);
        int off = r.getInt(h + 4 + 4 * first);
        byte[] overflow = raw.clone();

        for (int i = 0; i < 4; i++) {
            overflow[h + off + i] = (byte) 0xff;
        }

        overflow[h + off + 4] = 0x10;
        rejectsWrappedFix(in, overflow);
        byte[] unknown = raw.clone();
        unknown[h + off] = 0x42;
        rejectsWrappedFix(in, unknown);
        rejectsWrappedFix(in, reorderedMembers(raw));
        System.out.println(
                "PASS numeric flags preserved, streams packed in class-def order, other"
                        + " sections/map unchanged; SHA1+Adler; duplicate/missing/actual member"
                        + " category change/offset/ULEB overflow/unknown flag rejected");
    }

    static byte[] reorderedMembers(byte[] raw) throws Exception {
        var dex = new DexBackedDexFile(Opcodes.forApi(30), raw);
        List<ClassDef> altered = new ArrayList<>();
        boolean changed = false;

        for (ClassDef c : dex.getClasses()) {
            if (!changed && c.getType().contains("7coWc0tjIUC7grCXucNFbpYTxDI")) {
                List<Method> direct = new ArrayList<>(), virt = new ArrayList<>();
                c.getDirectMethods().forEach(direct::add);
                c.getVirtualMethods().forEach(virt::add);
                need(!direct.isEmpty());
                virt.add(direct.remove(0));
                altered.add(
                        new ImmutableClassDef(
                                c.getType(),
                                c.getAccessFlags(),
                                c.getSuperclass(),
                                c.getInterfaces(),
                                c.getSourceFile(),
                                c.getAnnotations(),
                                c.getStaticFields(),
                                c.getInstanceFields(),
                                direct,
                                virt));
                changed = true;
            } else {
                altered.add(ImmutableClassDef.of(c));
            }
        }

        need(changed);

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), altered));
    }
}
