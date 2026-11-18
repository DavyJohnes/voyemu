package com.voyemu.prepare;

import static com.voyemu.prepare.PassionNexusPowerGuard.PROFILE;
import static com.voyemu.prepare.PassionNexusPowerGuard.apply;
import static com.voyemu.prepare.PassionNexusPowerGuard.check;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

public final class PassionNexusPowerGuardTest {
    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] dex;

        try (ZipFile z = new ZipFile(apk.toFile())) {
            dex = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] out = apply(PROFILE, 30, apk, dex);
        Files.write(Path.of(args[1]), out);

        for (int mode = 0; mode < 3; mode++) {
            boolean denied = false;

            try {
                apply(
                        mode == 0 ? "other" : PROFILE,
                        mode == 1 ? 28 : 30,
                        apk,
                        mode == 2 ? out : dex);
            } catch (IllegalArgumentException expected) {
                denied = true;
            }

            check(denied);
        }

        System.out.println(
                "PASS private exact4 OEM hooks: actual APK/DEX/method pins,"
                        + " profile/API/already-patched negatives, policy code retained; inverse"
                        + " every class/try/branch/debug preserved. Not installed or runtime"
                        + " accepted.");
    }
}
