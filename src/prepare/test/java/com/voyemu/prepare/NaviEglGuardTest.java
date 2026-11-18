package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.zip.ZipFile;

public final class NaviEglGuardTest {
    static void check(boolean v, String m) {
        if (!v) {
            throw new AssertionError(m);
        }
    }

    interface Checked {
        void run() throws Exception;
    }

    static void rejects(Checked c) throws Exception {
        try {
            c.run();
            throw new AssertionError("wrong EGL identity accepted");
        } catch (Failure f) {
            check(f.code.equals("NAVI_EGL_CONTRACT"), "wrong rejection");
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] before;

        try (var z = new ZipFile(apk.toFile())) {
            before = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        int at = NaviEglGuard.offset(before);
        byte[] after = NaviEglGuard.apply(NaviEglGuard.PROFILE, 30, "arm64-v8a", apk, before);
        var b = ByteBuffer.wrap(after).order(ByteOrder.LITTLE_ENDIAN);
        check(
                b.getInt(at + 9 * 4) == 0 && b.getInt(at + 11 * 4) == 0,
                "RED: unsupported MSAA4 must not leave zero real EGL configurations");

        for (int i = 32; i < after.length; i++) {
            if (i != at + 9 * 4 && i != at + 11 * 4) {
                check(after[i] == before[i], "unrelated DEX byte changed");
            }
        }

        rejects(() -> NaviEglGuard.validate("foreign", 30, "arm64-v8a", apk, before));
        rejects(() -> NaviEglGuard.validate(NaviEglGuard.PROFILE, 28, "arm64-v8a", apk, before));
        rejects(() -> NaviEglGuard.validate(NaviEglGuard.PROFILE, 30, "x86_64", apk, before));
        rejects(() -> NaviEglGuard.validate(NaviEglGuard.PROFILE, 30, "arm64-v8a", apk, after));
        System.out.println(
                "PASS exact MSAA0 configuration; OEM renderer/unrelated bytes preserved;"
                        + " identity/reapply guards");
    }
}
