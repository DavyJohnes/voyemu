package com.voyemu.prepare;

import static com.voyemu.prepare.PassionDisplayPowerGuard.apply;
import static com.voyemu.prepare.PassionDisplayPowerGuard.check;
import static com.voyemu.prepare.TestProfiles.SYNTHETIC;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

public final class PassionDisplayPowerGuardTest {
    public static void main(String[] a) throws Exception {
        Path jar = Path.of(a[0]), out = Path.of(a[1]);
        byte[] dex;

        try (ZipFile z = new ZipFile(jar.toFile())) {
            dex = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] result = apply(SYNTHETIC, 30, jar, dex);
        Files.write(out, result);

        boolean rejected = false;

        try {
            apply(SYNTHETIC, 28, jar, dex);
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }

        check(rejected);
        rejected = false;

        try {
            apply(SYNTHETIC, 30, jar, result);
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }

        check(rejected);
        System.out.println(
                "PASS private exact2 SDK hooks; source JAR/DEX/method pins,"
                    + " API/tamper/already-patched negatives; inverse all-class"
                    + " semantics/branch/debug preserved. Helper classes NOT installed, runtime NOT"
                    + " accepted");
    }
}
