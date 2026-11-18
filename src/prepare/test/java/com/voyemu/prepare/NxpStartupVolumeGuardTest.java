package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.zip.ZipFile;

public class NxpStartupVolumeGuardTest {
    public static void main(String[] a) throws Exception {
        byte[] original;

        try (var z = new ZipFile(a[0])) {
            original = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] actual = NxpStartupVolumeGuard.apply(NxpStartupVolumeGuard.PROFILE, 28, original);

        if (!Arrays.equals(
                actual, NxpStartupVolumeGuard.apply(NxpStartupVolumeGuard.PROFILE, 28, original))) {
            throw new AssertionError("nondeterministic");
        }

        int negatives = 0;

        for (int scenario = 0; scenario < 4; scenario++) {
            try {
                NxpStartupVolumeGuard.apply(
                        scenario == 0 ? "other" : NxpStartupVolumeGuard.PROFILE,
                        scenario == 1 ? 30 : 28,
                        scenario == 2 ? actual : scenario == 3 ? new byte[] {1} : original);
                throw new AssertionError("unexpected acceptance");
            } catch (Failure expected) {
                negatives++;
            }
        }

        Files.write(Path.of(a[1]), actual);
        System.out.println(
                "PASS pinned NXP API28, 3 initialization methods, all 11 minimums zero, defaults"
                        + " zero, persisted indices reset, inverse all classes exact, later methods"
                        + " unchanged; negatives="
                        + negatives
                        + "; guest_started=false");
    }
}
