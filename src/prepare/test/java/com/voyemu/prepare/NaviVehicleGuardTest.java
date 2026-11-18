package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.zip.ZipFile;

public final class NaviVehicleGuardTest {
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
            throw new AssertionError("guard accepted wrong identity");
        } catch (Failure f) {
            check(f.code.equals("NAVI_VEHICLE_CONTRACT"), "wrong failure");
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] original;

        try (var z = new ZipFile(apk.toFile())) {
            original = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        int at = NaviVehicleGuard.offset(original);
        check(original[at] == 0x71, "RED: original queries inaccessible NVRAM before factory");
        byte[]
                version =
                        NaviVersionGuard.apply(
                                NaviVehicleGuard.PROFILE, 30, "arm64-v8a", apk, original),
                after =
                        NaviVehicleGuard.apply(
                                NaviVehicleGuard.PROFILE, 30, "arm64-v8a", apk, version);
        check(
                Arrays.equals(
                        Arrays.copyOfRange(after, at, at + 8),
                        new byte[] {0x13, 0, (byte) 134, 0, 0, 0, 0, 0}),
                "GREEN: exact H97C 134 must reach the original OEM factory");

        for (int i = 32; i < after.length; i++) {
            if (i < at || i >= at + 8) {
                check(after[i] == version[i], "unrelated DEX change");
            }
        }

        rejects(() -> NaviVehicleGuard.validate("foreign", 30, "arm64-v8a", apk, original));
        rejects(() ->
                NaviVehicleGuard.validate(
                        NaviVehicleGuard.PROFILE, 28, "arm64-v8a", apk, original));
        rejects(() ->
                NaviVehicleGuard.validate(NaviVehicleGuard.PROFILE, 30, "x86_64", apk, original));
        rejects(() ->
                NaviVehicleGuard.validate(NaviVehicleGuard.PROFILE, 30, "arm64-v8a", apk, after));
        System.out.println(
                "PASS Free A11 7.3.1 H97C factory RED/GREEN; unrelated bytes preserved;"
                        + " profile/API/ABI/reapply denied");
    }
}
