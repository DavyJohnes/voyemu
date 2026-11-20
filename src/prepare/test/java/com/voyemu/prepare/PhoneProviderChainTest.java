package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class PhoneProviderChainTest {
    /** Pins of two BluetoothPhone originals of different firmware. */
    static final String PIN = "c3b53187d5161692a338a9504e1fa288dd163896da431bf89b8ad1e8fa56b1f4",
            OTHER_PIN = "c8a33e377a6a708ef6c3cca7e8cc3f938349c04dd494dac32bcf823a3096efa1";

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
        Path apk611 =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, PIN)
                                + "/originals/BluetoothPhone.apk");
        Path apk731 =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, OTHER_PIN)
                                + "/originals/BluetoothPhone.apk");
        Map<String, Object> constants = TestProfiles.constants(root, PhoneProviderGuard.NAME, PIN),
                other = TestProfiles.constants(root, PhoneProviderGuard.NAME, OTHER_PIN);
        byte[] raw611 = NativeCore.entries(apk611).get("classes.dex");
        byte[] raw731 = NativeCore.entries(apk731).get("classes.dex");

        PhoneConsumerContract.verify(PhoneConsumerContract.read(apk611));
        byte[] window = PhoneCallWindowGuard.apply(constants, 30, apk611, raw611);
        PhoneCallWindowGuard.verify(constants, raw611, window);
        byte[] fixed = PhoneDisconnectGuard.apply(constants, 30, apk611, window);
        PhoneDisconnectGuard.verify(constants, apk611, raw611, fixed);
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
        rejects(() -> PhoneCallWindowGuard.apply(other, 30, apk611, raw611));
        rejects(() -> PhoneCallWindowGuard.apply(constants, 30, apk731, raw731));
        rejects(() -> PhoneCallWindowGuard.apply(other, 30, apk611, raw611));
        rejects(() -> PhoneDisconnectGuard.apply(constants, 30, apk611, raw611));
        byte[] drift = fixed.clone();
        drift[drift.length - 1] ^= 1;
        rejects(() -> PhoneDisconnectGuard.verify(constants, apk611, raw611, drift));
    }
}
