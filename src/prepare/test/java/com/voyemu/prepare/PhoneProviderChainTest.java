package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;

public final class PhoneProviderChainTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1",
            OTHER = "free-11-chinese-20260731-7.3.1";

    interface Check {
        void run() throws Exception;
    }

    static void rejects(Check action) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("Phone guard accepted an incompatible input");
    }

    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Path apk611 = root.resolve(".local/profiles/" + PROFILE + "/originals/BluetoothPhone.apk");
        Path apk731 = root.resolve(".local/profiles/" + OTHER + "/originals/BluetoothPhone.apk");
        byte[] raw611 = NativeCore.entries(apk611).get("classes.dex");
        byte[] raw731 = NativeCore.entries(apk731).get("classes.dex");

        PhoneConsumerContract.verify(PhoneConsumerContract.read(apk611));
        byte[] window = PhoneCallWindowGuard.apply(PROFILE, 30, apk611, raw611);
        PhoneCallWindowGuard.verify(PROFILE, raw611, window);
        byte[] fixed = PhoneDisconnectGuard.apply(PROFILE, 30, apk611, window);
        PhoneDisconnectGuard.verify(PROFILE, apk611, raw611, fixed);
        need(
                PhoneDisconnectGuardTest.suffix(window, null) == null,
                "original getter must reproduce null");
        Object empty = PhoneDisconnectGuardTest.suffix(fixed, null);
        need(
                empty instanceof List && ((List<?>) empty).isEmpty(),
                "fixed getter must return empty list");
        var calls = new java.util.ArrayList<>(List.of("call"));
        // The fixed getter must return the caller's snapshot object itself.
        @SuppressWarnings("ReferenceEquality")
        boolean retained = PhoneDisconnectGuardTest.suffix(fixed, calls) == calls;
        need(retained, "fixed getter must retain snapshot");
        rejects(() -> PhoneCallWindowGuard.apply(OTHER, 30, apk611, raw611));
        rejects(() -> PhoneCallWindowGuard.apply(PROFILE, 30, apk731, raw731));
        rejects(() -> PhoneCallWindowGuard.apply(OTHER, 30, apk611, raw611));
        rejects(() -> PhoneDisconnectGuard.apply(PROFILE, 30, apk611, raw611));
        byte[] drift = fixed.clone();
        drift[drift.length - 1] ^= 1;
        rejects(() -> PhoneDisconnectGuard.verify(PROFILE, apk611, raw611, drift));
    }
}
