package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.zip.ZipFile;

public final class HiboardGuardChainTest {
    static void check(boolean ok, String reason) {
        if (!ok) {
            throw new AssertionError(reason);
        }
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        try {
            action.run();
            throw new AssertionError("unsupported Hiboard input accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    static byte[] dex(Path p) throws Exception {
        try (var z = new ZipFile(p.toFile())) {
            return z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException("611_APK 731_APK");
        }

        Path apk = Path.of(args[0]), other = Path.of(args[1]);
        byte[] original = dex(apk), foreign = dex(other);
        String pin = FilesSupport.hash(apk);
        Map<String, Object> constants =
                TestProfiles.constants(Path.of("").toAbsolutePath(), "api30-hiboard-startup", pin);
        HiboardRouting.validate(constants, 30, apk, original);
        check(HiboardRouting.bodies(constants).size() == 28, "exact routing method inventory");

        for (int n = 10; n <= 12; n++) {
            String key = "Lcom/qinggan/app/hiboard/system/HiboardBar$" + n + ";->run()V";
            check(
                    HiboardRouting.bodies(constants).stream().anyMatch(b -> b.method().equals(key)),
                    "renumbered callback missing");
        }

        HiboardStartupGuard.validateOriginalChain(constants, 30, apk, original);
        byte[] routed = HiboardRouting.apply(constants, 30, apk, original);
        HiboardStartupGuard.validateRouted(constants, 30, apk, routed);
        byte[] result = HiboardStartupGuard.apply(constants, 30, apk, routed);
        check(
                !Arrays.equals(original, routed) && !Arrays.equals(routed, result),
                "routing and startup must change DEX");
        reject(() -> HiboardRouting.validate(constants, 28, apk, original));
        reject(() -> HiboardRouting.validate(constants, 30, other, foreign));
        reject(() -> HiboardRouting.validate(constants, 30, apk, foreign));
        reject(() -> HiboardRouting.validate(constants, 30, apk, routed));
        reject(() -> HiboardStartupGuard.validateRouted(constants, 30, apk, original));
        reject(() -> HiboardStartupGuard.validateRouted(constants, 30, apk, result));
        check(FilesSupport.hash(apk).equals(pin), "OEM input changed");
        System.out.println(
                "611 Hiboard route and startup chain rejects foreign and reordered inputs;"
                        + " no guest acceptance");
    }
}
