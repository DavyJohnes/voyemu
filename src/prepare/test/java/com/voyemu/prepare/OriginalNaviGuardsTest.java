package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;

public final class OriginalNaviGuardsTest {
    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    interface Run {
        void go() throws Exception;
    }

    static void rejects(Run r) throws Exception {
        try {
            r.go();
            throw new AssertionError("invalid input accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    static String hash(byte[] b) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(b));
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 2);
        byte[] egl = Files.readAllBytes(Path.of(args[0])),
                services = Files.readAllBytes(Path.of(args[1]));
        rejects(() -> OriginalNaviEglGuard.apply(TestProfiles.SYNTHETIC, 29, egl));
        byte[] eglo = OriginalNaviEglGuard.apply(TestProfiles.SYNTHETIC, 30, egl);
        need(hash(eglo).equals("765f698d9dcf8dcfbf75dfad20bdf537828eaa0916f2d94bb2859444b1535446"));
        rejects(() -> OriginalNaviEglGuard.apply(TestProfiles.SYNTHETIC, 30, eglo));
        rejects(() -> OriginalNaviLibrariesGuard.apply(TestProfiles.SYNTHETIC, 29, services));
        byte[] so = OriginalNaviLibrariesGuard.apply(TestProfiles.SYNTHETIC, 30, services);
        need(hash(so).equals("b4a79adb0df67394ed211a2ac2bd22055f6727d350185b73bf92d387ed8d81fe"));
        rejects(() -> OriginalNaviLibrariesGuard.apply(TestProfiles.SYNTHETIC, 30, so));
        System.out.println(
                "PASS pinned actual DEX, full semantic/raw flags, scope and reapply/wrong"
                        + " negatives");
    }
}
