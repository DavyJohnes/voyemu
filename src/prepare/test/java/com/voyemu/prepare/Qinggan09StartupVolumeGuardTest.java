package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Set;
import java.util.zip.ZipFile;

/** The two API28 profiles share exactly one pinned SDK services DEX. */
public final class Qinggan09StartupVolumeGuardTest {
    public static void main(String[] args) throws Exception {
        byte[] original;

        try (var jar = new ZipFile(args[0])) {
            original = jar.getInputStream(jar.getEntry("classes.dex")).readAllBytes();
        }

        String a9 = TestProfiles.SYNTHETIC;
        byte[] patched = NxpStartupVolumeGuard.apply(a9, 28, original);
        check(
                Arrays.equals(
                        patched, NxpStartupVolumeGuard.apply(TestProfiles.SYNTHETIC, 28, original)),
                "Same pinned SDK must produce an identical patch");
        var before = PassionServiceForegroundGuard.classes(original);
        var after = PassionServiceForegroundGuard.classes(patched);
        var inverse = NxpStartupVolumeGuard.transform(after, true);
        check(before.keySet().equals(after.keySet()), "No original class may disappear");
        int changed = 0;

        for (String key : before.keySet()) {
            check(
                    FrameworkDexMaterializer.semantic(before.get(key))
                            .equals(FrameworkDexMaterializer.semantic(inverse.get(key))),
                    "Exact inverse " + key);

            if (!FrameworkDexMaterializer.semantic(before.get(key))
                    .equals(FrameworkDexMaterializer.semantic(after.get(key)))) {
                check(
                        Set.of(NxpStartupVolumeGuard.AUDIO, NxpStartupVolumeGuard.STREAM)
                                .contains(key),
                        "Unrelated class changed " + key);
                changed++;
            }
        }

        check(changed == 2, "Exactly AudioService and VolumeStreamState change");
        int negatives = 0;

        reject(a9, 30, original);
        negatives++;
        reject(a9, 28, patched);
        negatives++;
        Files.write(Path.of(args[1]), patched);
        System.out.println(
                "PASS pinned API28 SDK guard; unchanged unrelated classes; inverse exact;"
                        + " negatives="
                        + negatives
                        + "; guest_started=false");
    }

    private static void reject(String profile, int api, byte[] dex) throws Exception {
        try {
            NxpStartupVolumeGuard.apply(profile, api, dex);
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("Unexpected guard acceptance " + profile);
    }

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }
}
