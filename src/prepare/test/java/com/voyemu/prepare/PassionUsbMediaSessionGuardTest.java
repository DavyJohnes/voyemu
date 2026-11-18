package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.zip.ZipFile;

public final class PassionUsbMediaSessionGuardTest {
    interface Checked {
        void run() throws Exception;
    }

    static void rejects(Checked c) throws Exception {
        try {
            c.run();
            throw new AssertionError("guard accepted invalid input");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]),
                source = Path.of(args[1]),
                platform = Path.of(args[2]),
                work = Path.of(args[3]);
        byte[] raw;

        try (var zip = new ZipFile(apk.toFile())) {
            raw = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }

        String profile = PassionUsbMediaSessionGuard.PROFILE;
        PassionUsbMediaSessionGuard.validateOriginalChain(profile, 30, apk, raw);
        byte[] foreground = PassionMusicForegroundGuard.apply(profile, 30, apk, raw),
                prepared = PassionMusicInsetsGuard.apply(profile, 30, apk, foreground);
        rejects(() -> PassionUsbMediaSessionGuard.validate("wrong", 30, apk, prepared));
        rejects(() -> PassionUsbMediaSessionGuard.validate(profile, 29, apk, prepared));
        byte[] patched = PassionUsbMediaSessionGuard.apply(profile, 30, apk, prepared);
        rejects(() -> PassionUsbMediaSessionGuard.apply(profile, 30, apk, patched));
        PassionUsbMediaSessionGuard.compileHelper(profile, 30, apk, source, platform, work);
        System.out.println(
                "OEM exact guard, inverse, wrong profile/API/tamper/duplicate and helper DEX PASS");
    }
}
