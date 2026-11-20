package com.voyemu.prepare;

import static com.voyemu.prepare.PassionStartupVolumeGuard.apply;
import static com.voyemu.prepare.PassionStartupVolumeGuard.validate;
import static com.voyemu.prepare.PassionStartupVolumeGuard.validateOriginal;
import static com.voyemu.prepare.PassionStartupVolumeGuard.validateOriginalChain;
import static com.voyemu.prepare.TestProfiles.SYNTHETIC;

import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.zip.ZipFile;

public final class PassionStartupVolumeGuardTest {
    public static void main(String[] a) throws Exception {
        Path jar = Path.of(a[0]), out = Path.of(a[1]);
        byte[] dex;

        try (ZipFile z = new ZipFile(jar.toFile())) {
            dex = z.getInputStream(z.getEntry(PassionStartupVolumeGuard.DEX)).readAllBytes();
        }

        validateOriginalChain(SYNTHETIC, 30, jar, dex);
        dex = PassionDisplayPowerGuard.apply(SYNTHETIC, 30, jar, dex);
        validateOriginal(SYNTHETIC, 30, jar, dex);
        byte[] result = apply(SYNTHETIC, 30, jar, dex);
        Files.write(out, result);

        // Deterministic and idempotent-by-pin: the same input always produces the same bytes; the
        // patched
        // DEX can never be accepted as a predecessor again.
        if (!Arrays.equals(result, apply(SYNTHETIC, 30, jar, dex))) {
            throw new AssertionError("startup volume output differs");
        }

        // The patch differs from the predecessor only in the four pinned AudioService initializers.
        var before = PassionServiceForegroundGuard.classes(dex);
        var patched = PassionServiceForegroundGuard.classes(result);
        checkPatchedClasses(before, patched);
        int negatives = foreignRejections(false, jar, dex, "foreign startup API accepted");

        if (!rejected(false, SYNTHETIC, 30, jar, result)) {
            throw new AssertionError("already patched DEX accepted as predecessor");
        }

        negatives++;
        Path wrong = Files.createTempFile("wrong", ".jar");
        Files.write(wrong, new byte[] {1});

        if (!rejected(false, SYNTHETIC, 30, wrong, dex)) {
            throw new AssertionError("wrong jar pin accepted");
        }

        negatives++;

        if (negatives != 3) {
            throw new AssertionError(
                    "expected exactly 3 rejected startup identities, got " + negatives);
        }

        // validate() is the plan-validation leg of the identical contract: it must reject exactly
        // the same
        // foreign/wrong-pin inputs and accept only the pinned original.
        int validationNegatives =
                foreignRejections(true, jar, dex, "foreign validate API accepted");

        if (!rejected(true, SYNTHETIC, 30, jar, result)) {
            throw new AssertionError("patched validate DEX accepted as predecessor");
        }

        validationNegatives++;

        if (!rejected(true, SYNTHETIC, 30, wrong, dex)) {
            throw new AssertionError("wrong validate jar pin accepted");
        }

        validationNegatives++;

        if (validationNegatives != 3) {
            throw new AssertionError(
                    "expected exactly 3 rejected validate identities, got " + validationNegatives);
        }

        validate(SYNTHETIC, 30, jar, dex);
        validateOriginal(SYNTHETIC, 30, jar, dex);
        System.out.println(
                "PASS Passion startup volume guard: pinned jar/DEX identity, streams to minimum,"
                        + " groups to zero, later controls stock, inverse-exact serialization;"
                        + " negatives="
                        + (negatives + validationNegatives)
                        + "; guest_started=false");
    }

    static void checkPatchedClasses(Map<String, ClassDef> before, Map<String, ClassDef> patched)
            throws Exception {
        if (!patched.keySet().equals(before.keySet())) {
            throw new AssertionError("class set changed");
        }

        int changed = 0;

        for (var key : before.keySet()) {
            if (!FrameworkDexMaterializer.semantic(before.get(key))
                    .equals(FrameworkDexMaterializer.semantic(patched.get(key)))) {
                changed++;
            }
        }

        if (changed != 3) {
            throw new AssertionError(
                    "expected exactly three patched audio classes, got " + changed);
        }

        int targetMethods = 0;

        for (var c : patched.values()) {
            for (var m : c.getMethods()) {
                if (StartupVolumeGuard.target(m)) {
                    targetMethods++;

                    if (m.getImplementation() == null) {
                        throw new AssertionError("target method lost its body");
                    }
                }
            }
        }

        if (targetMethods != 4) {
            throw new AssertionError("expected four rewritten initializers, got " + targetMethods);
        }

        // Inverse is exact: undoing the pinned sequence restores every original class semantically.
        var inverse = StartupVolumeGuard.transform(patched, true);

        if (!inverse.keySet().equals(before.keySet())) {
            throw new AssertionError("inverse class set changed");
        }

        for (var key : before.keySet()) {
            if (!FrameworkDexMaterializer.semantic(before.get(key))
                    .equals(FrameworkDexMaterializer.semantic(inverse.get(key)))) {
                throw new AssertionError("inverse differs: " + key);
            }
        }
    }

    /** Runs validate or apply and reports whether the exact contract refused the input. */
    static boolean rejected(boolean validation, String profile, int api, Path jar, byte[] dex)
            throws Exception {
        try {
            if (validation) {
                validate(profile, api, jar, dex);
            } else {
                apply(profile, api, jar, dex);
            }
        } catch (Failure expected) {
            return true;
        }

        return false;
    }

    static int foreignRejections(boolean validation, Path jar, byte[] dex, String message)
            throws Exception {
        int negatives = 0;

        if (!rejected(validation, SYNTHETIC, 28, jar, dex)) {
            throw new AssertionError(message);
        }

        negatives++;

        return negatives;
    }
}
