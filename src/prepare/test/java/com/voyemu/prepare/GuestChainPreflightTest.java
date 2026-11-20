package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public class GuestChainPreflightTest {
    static byte[] dex(Path p, String n) throws Exception {
        try (var z = new ZipFile(p.toFile())) {
            return z.getInputStream(z.getEntry(n)).readAllBytes();
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), out = Path.of(a[1]);
        var checks = new LinkedHashMap<String, Object>();

        for (String role : List.of("LY_Car", "LY_AVM", "LY_Launcher")) {
            Path original =
                    root.resolve(
                            ".local/profiles/"
                                    + TestProfiles.pinning(root, PassionGuestNullGuards.ECU.apk())
                                    + "/originals/"
                                    + role
                                    + ".apk");
            byte[] raw =
                    dex(original, role.equals("LY_Launcher") ? "classes3.dex" : "classes2.dex");
            String hash = NativeCore.hash(raw);
            long start = System.nanoTime();

            if (role.equals("LY_Launcher")) {
                PassionNavigationReadyGuard.validateOriginalChain(
                        TestProfiles.SYNTHETIC, 30, original, raw);
            } else {
                PassionGuestNullGuards.validateOriginalChain(
                        TestProfiles.SYNTHETIC,
                        30,
                        original,
                        raw,
                        role.equals("LY_Car")
                                ? PassionGuestNullGuards.ECU
                                : PassionGuestNullGuards.AVM);
            }

            if (!hash.equals(NativeCore.hash(raw))) {
                throw new AssertionError("raw entry mutated");
            }

            checks.put(
                    role,
                    Map.of(
                            "raw_unchanged",
                            true,
                            "seconds",
                            (System.nanoTime() - start) / 1e9,
                            "raw_sha256",
                            hash));
            System.out.println(role + " actual predecessor chain pass0 read-only PASS");
        }

        Files.writeString(
                out.resolve("chain-preflight-receipt.json"),
                Json.write(
                                Map.of(
                                        "checks",
                                        checks,
                                        "FULLArtifacts_integration",
                                        false,
                                        "canonical_changed",
                                        false,
                                        "runtime",
                                        false))
                        + "\n");
    }
}
